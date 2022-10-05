#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <aslblock.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <topology.h>

#include "libaslblock.h"
#include "interpose.h"
#include "utils.h"

#include "waiting_policy.h"

/* Default Number */
#define DEFAULT_REORDER         1000
#define MAX_REORDER             1000000000
/* #define MAX_REORDER             22000 */
#define DEFAULT_ADJUST_UNIT	    100
#define MIN_ADJUST_UNIT		    10
#define REORDER_THRESHOLD       1000
#define EPOCH_REQ_THRESHOLD	    100
__thread reorder_time = 20000000;
extern __thread cur_thread_id;
/* Epoch Information */
#define MAX_EPOCH	256
typedef struct {
    uint64_t reorder_window;
    uint64_t adjust_unit;
    struct timespec start_ts;
} epoch_t;

__thread epoch_t epoch[MAX_EPOCH] = { 0 };
__thread int cur_epoch_id = -1;

void *aslblock_alloc_cache_align(size_t n)
{
    void *res = 0;
    if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res) {
        fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
                (unsigned long long)cache_align(n));
        exit(-1);
    }
    return res;
}

uint64_t get_current_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000000LL + now.tv_nsec;
}

aslblock_mutex_t *aslblock_mutex_create(const pthread_mutexattr_t * attr)
{
    aslblock_mutex_t *impl =
        (aslblock_mutex_t *) aslblock_alloc_cache_align(sizeof(aslblock_mutex_t));
    impl->tail = 0;
#if COND_VAR
    REAL(pthread_mutex_init) (&impl->posix_lock, /*&errattr */ attr);
#endif
    return impl;
}

static int __aslblock_mutex_trylock(aslblock_mutex_t * impl, aslblock_node_t * me)
{
    aslblock_node_t *expected;
    assert(me != NULL);
    me->next = NULL;
    expected = NULL;
    return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
            __ATOMIC_ACQ_REL,
            __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __aslblock_lock_fifo(aslblock_mutex_t * impl, aslblock_node_t * me)
{
    aslblock_node_t *tail;
    me->next = NULL;
    tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
    if (tail) {
        me->spin = 0;
        __atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
        while (me->spin == 0)
            CPU_PAUSE();
    }
    MEMORY_BARRIER();
    return 0;
}

static inline int __aslblock_lock_reorder(aslblock_mutex_t * impl, aslblock_node_t * me,
        uint64_t reorder_window)
{
    uint64_t reorder_window_ddl;
    uint64_t current_ns;
    uint32_t cnt = 0, next_check = 100;
    uint64_t sleep_time = 2000;
    /* Fast Path */
    // printf("dd reorder %d\n", reorder_window);
    if (impl->tail == NULL) {
        // reorder_time -= 100;
        return __aslblock_lock_fifo(impl, me);
    }
        

    /* Someone is holding the lock */
    reorder_window_ddl = get_current_ns() + reorder_time;
    while ((current_ns = get_current_ns()) < reorder_window_ddl) {
            sleep_time = sleep_time < reorder_window_ddl - current_ns ?
                sleep_time : reorder_window_ddl - current_ns;
            // printf("tid %d sleep %d window %d\n", cur_thread_id, sleep_time, reorder_window);
            nanosleep((const struct timespec[]){{0, sleep_time}}, NULL);
            if (impl->tail == NULL) {
                // reorder_time -= 100;
                return __aslblock_lock_fifo(impl, me);
            }
                
            // sleep_time = sleep_time << 1;
        } 
        // reorder_time += 100;
    return __aslblock_lock_fifo(impl, me);
}

static inline int __aslblock_lock_eventually(aslblock_mutex_t * impl, aslblock_node_t * me)
{
    return __aslblock_lock_reorder(impl, me, MAX_REORDER);
}

static int __aslblock_mutex_lock(aslblock_mutex_t * impl, aslblock_node_t * me)
{
        return __aslblock_lock_reorder(impl, me,
                epoch[cur_epoch_id].reorder_window);
}

int aslblock_mutex_lock(aslblock_mutex_t * impl, aslblock_node_t * me)
{
    int ret = __aslblock_mutex_lock(impl, me);
    assert(ret == 0);
#if COND_VAR
    assert(REAL(pthread_mutex_lock) (&impl->posix_lock) == 0);
#endif
    return ret;
}

int aslblock_mutex_trylock(aslblock_mutex_t * impl, aslblock_node_t * me)
{
    if (!__aslblock_mutex_trylock(impl, me)) {
#if COND_VAR
        REAL(pthread_mutex_lock) (&impl->posix_lock);
#endif
        return 0;
    }
    return -EBUSY;
}

static void __aslblock_mutex_unlock(aslblock_mutex_t * impl, aslblock_node_t * me)
{
    aslblock_node_t *expected;
    if (!me->next) {
        expected = me;
        if (__atomic_compare_exchange_n(&impl->tail, &expected, 0, 0,
                    __ATOMIC_RELEASE,
                    __ATOMIC_RELAXED)) {
            goto out;
        }
        while (!me->next)
            CPU_PAUSE();
    }
    MEMORY_BARRIER(); 
    me->next->spin = 1;
out:
    return;
}

void aslblock_mutex_unlock(aslblock_mutex_t * impl, aslblock_node_t * me)
{
#if COND_VAR
    assert(REAL(pthread_mutex_unlock) (&impl->posix_lock) == 0);
#endif
    __aslblock_mutex_unlock(impl, me);
}

int aslblock_mutex_destroy(aslblock_mutex_t * lock)
{
#if COND_VAR
    REAL(pthread_mutex_destroy) (&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int aslblock_cond_init(aslblock_cond_t * cond, const pthread_condattr_t * attr)
{
#if COND_VAR
    return REAL(pthread_cond_init) (cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int aslblock_cond_timedwait(aslblock_cond_t * cond, aslblock_mutex_t * lock,
        aslblock_node_t * me, const struct timespec *ts)
{
#if COND_VAR
    int res;

    __aslblock_mutex_unlock(lock, me);

    if (ts)
        res =
            REAL(pthread_cond_timedwait) (cond, &lock->posix_lock, ts);
    else
        res = REAL(pthread_cond_wait) (cond, &lock->posix_lock);

    if (res != 0 && res != ETIMEDOUT) {
        fprintf(stderr, "Error on cond_{timed,}wait %d\n", res);
        assert(0);
    }

    int ret = 0;
    if ((ret = REAL(pthread_mutex_unlock) (&lock->posix_lock)) != 0) {
        fprintf(stderr, "Error on mutex_unlock %d\n", ret == EPERM);
        assert(0);
    }

    aslblock_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int aslblock_cond_wait(aslblock_cond_t * cond, aslblock_mutex_t * lock, aslblock_node_t * me)
{
    return aslblock_cond_timedwait(cond, lock, me, 0);
}

int aslblock_cond_signal(aslblock_cond_t * cond)
{
#if COND_VAR
    return REAL(pthread_cond_signal) (cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int aslblock_cond_broadcast(aslblock_cond_t * cond)
{
#if COND_VAR
    return REAL(pthread_cond_broadcast) (cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int aslblock_cond_destroy(aslblock_cond_t * cond)
{
#if COND_VAR
    return REAL(pthread_cond_destroy) (cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void aslblock_thread_start(void)
{
    for (int i = 0; i < MAX_EPOCH; i++) {
        epoch[i].reorder_window = DEFAULT_REORDER;
        epoch[i].adjust_unit = DEFAULT_ADJUST_UNIT;
    }
    cur_epoch_id = -1;
}

void aslblock_thread_exit(void)
{
}

void aslblock_application_init(void)
{
}

void aslblock_application_exit(void)
{
}

void aslblock_init_context(lock_mutex_t * UNUSED(impl),
        lock_context_t * UNUSED(context), int UNUSED(number))
{
}

/* A stack to implement nested epoch */
#define MAX_DEPTH 30
__thread int epoch_stack[MAX_DEPTH];
__thread int stack_pos = -1;

/* Per-thread private stack */
int push_epoch(int epoch_id)
{
    stack_pos++;
    if (stack_pos == MAX_EPOCH)
        return -ENOSPC;
    epoch_stack[stack_pos] = epoch_id;
    return 0;
}

/* Per-thread private stack */
int pop_epoch(void)
{
    if (stack_pos < 0)
        return -EINVAL;
    return epoch_stack[stack_pos--];
}

int is_stack_empty(void)
{
    return stack_pos < 0;
}

/* New interfaces in LibASL */

/* Epoch-based interface */
int epoch_start(int epoch_id)
{
    if (epoch_id < 0 || epoch_id > MAX_EPOCH || cur_epoch_id < -1)
        return -EINVAL;
    if (cur_epoch_id != -1 && push_epoch(cur_epoch_id) < 0)
        return -ENOSPC;
    /* Set cur_epoch_id */
    cur_epoch_id = epoch_id;
    /* Get the epoch start time */
    clock_gettime(CLOCK_MONOTONIC, &epoch[cur_epoch_id].start_ts);
    return 0;
}

int epoch_end(int epoch_id, uint64_t required_latency)
{
    struct timespec epoch_end_ts;
    uint64_t duration = 0;
    uint64_t reorder_window = epoch[cur_epoch_id].reorder_window;
   
    /* Fast out */
    if (is_big_core())
        goto out;
    if (required_latency < EPOCH_REQ_THRESHOLD) {
        epoch[cur_epoch_id].reorder_window = 0;
        goto out;
    }
    if (epoch_id < 0 || epoch_id > MAX_EPOCH)
        return -EINVAL;
    if (epoch_id != cur_epoch_id)
        return -EINVAL;
    
    /* Get the epoch end time */
    clock_gettime(CLOCK_MONOTONIC, &epoch_end_ts);
    duration =
        (epoch_end_ts.tv_sec - epoch[cur_epoch_id].start_ts.tv_sec) * 1000000000LL +
        epoch_end_ts.tv_nsec - epoch[cur_epoch_id].start_ts.tv_nsec;
    /* Adjust the reorder window */
    if (duration > required_latency) {
        reorder_window >>= 1;
        epoch[cur_epoch_id].adjust_unit = reorder_window / 99;
        if (epoch[cur_epoch_id].adjust_unit < MIN_ADJUST_UNIT)
            epoch[cur_epoch_id].adjust_unit = MIN_ADJUST_UNIT;
    } else {
        reorder_window += epoch[cur_epoch_id].adjust_unit;
    }
    epoch[cur_epoch_id].reorder_window = reorder_window;
out:
    /* Support nested epoches */
    if (is_stack_empty())
        cur_epoch_id = -1;
    else
        cur_epoch_id = pop_epoch();
    return 0;
}
