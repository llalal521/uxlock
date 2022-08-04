#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <csupperbound.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <topology.h>

#include "libcsupper.h"
#include "interpose.h"
#include "utils.h"

#include "waiting_policy.h"

/* Default Number */
#define DEFAULT_REORDER         100
#define MAX_REORDER             1000000000
/* #define MAX_REORDER             22000 */
#define DEFAULT_ADJUST_UNIT	    100
#define MIN_ADJUST_UNIT		    10
#define REORDER_THRESHOLD       1000
#define EPOCH_REQ_THRESHOLD	    100

/* Epoch Information */
#define MAX_EPOCH	256
typedef struct {
    uint64_t reorder_window;
    uint64_t adjust_unit;
    struct timespec start_ts;
} epoch_t;

__thread epoch_t epoch[MAX_EPOCH] = { 0 };
__thread int cur_epoch_id = -1;
extern __thread unsigned int cur_thread_id;
__thread unsigned int uxthread;
__thread unsigned int cri_len;
void *csupperbound_alloc_cache_align(size_t n)
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

csupperbound_mutex_t *csupperbound_mutex_create(const pthread_mutexattr_t * attr)
{
    csupperbound_mutex_t *impl =
        (csupperbound_mutex_t *) csupperbound_alloc_cache_align(sizeof(csupperbound_mutex_t));
    impl->tail = 0;
#if COND_VAR
    REAL(pthread_mutex_init) (&impl->posix_lock, /*&errattr */ attr);
#endif
    return impl;
}

static int __csupperbound_mutex_trylock(csupperbound_mutex_t * impl, csupperbound_node_t * me)
{
    csupperbound_node_t *expected;
    assert(me != NULL);
    me->next = NULL;
    expected = NULL;
    return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
            __ATOMIC_ACQ_REL,
            __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __csupperbound_lock_fifo(csupperbound_mutex_t * impl, csupperbound_node_t * me)
{
    csupperbound_node_t *tail;
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

static inline int __csupperbound_lock_reorder(csupperbound_mutex_t * impl, csupperbound_node_t * me,
        uint64_t reorder_window)
{
    uint64_t reorder_window_ddl;
    uint64_t current_ns;
    uint32_t cnt = 0, next_check = 100;
    /* Fast Path */
    if (reorder_window < REORDER_THRESHOLD || impl->tail == NULL){
         return __csupperbound_lock_fifo(impl, me);
    }
       

    /* Someone is holding the lock */
    reorder_window_ddl = get_current_ns() + reorder_window;
    while ((current_ns = get_current_ns()) < reorder_window_ddl) {
        
        if (cnt++ == next_check) {
            /* Check the queue in a backoff manner */
            if (impl->tail == NULL){
                break;
            }
            next_check <<= 1;
        }
    }
    return __csupperbound_lock_fifo(impl, me);
}

static inline int __csupperbound_lock_eventually(csupperbound_mutex_t * impl, csupperbound_node_t * me)
{
    return __csupperbound_lock_reorder(impl, me, MAX_REORDER);
}


static int __csupperbound_mutex_lock(csupperbound_mutex_t * impl, csupperbound_node_t * me)
{   
    if (cri_len == 1){
        return __csupperbound_lock_fifo(impl, me);
    }
    else
    // else if (cur_epoch_id < 0 || cur_epoch_id > MAX_EPOCH)
        return __csupperbound_lock_eventually(impl, me);
    // else
    //     return __csupperbound_lock_reorder(impl, me,
    //             epoch[cur_epoch_id].reorder_window);
}

int csupperbound_mutex_lock(csupperbound_mutex_t * impl, csupperbound_node_t * me)
{
    int ret = __csupperbound_mutex_lock(impl, me);
    assert(ret == 0);
#if COND_VAR
    assert(REAL(pthread_mutex_lock) (&impl->posix_lock) == 0);
#endif
    return ret;
}

int csupperbound_mutex_trylock(csupperbound_mutex_t * impl, csupperbound_node_t * me)
{
    if (!__csupperbound_mutex_trylock(impl, me)) {
#if COND_VAR
        REAL(pthread_mutex_lock) (&impl->posix_lock);
#endif
        return 0;
    }
    return -EBUSY;
}

static void __csupperbound_mutex_unlock(csupperbound_mutex_t * impl, csupperbound_node_t * me)
{
    csupperbound_node_t *expected;
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

void csupperbound_mutex_unlock(csupperbound_mutex_t * impl, csupperbound_node_t * me)
{
#if COND_VAR
    assert(REAL(pthread_mutex_unlock) (&impl->posix_lock) == 0);
#endif
    __csupperbound_mutex_unlock(impl, me);
}

int csupperbound_mutex_destroy(csupperbound_mutex_t * lock)
{
#if COND_VAR
    REAL(pthread_mutex_destroy) (&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int csupperbound_cond_init(csupperbound_cond_t * cond, const pthread_condattr_t * attr)
{
#if COND_VAR
    return REAL(pthread_cond_init) (cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int csupperbound_cond_timedwait(csupperbound_cond_t * cond, csupperbound_mutex_t * lock,
        csupperbound_node_t * me, const struct timespec *ts)
{
#if COND_VAR
    int res;

    __csupperbound_mutex_unlock(lock, me);

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

    csupperbound_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int csupperbound_cond_wait(csupperbound_cond_t * cond, csupperbound_mutex_t * lock, csupperbound_node_t * me)
{
    return csupperbound_cond_timedwait(cond, lock, me, 0);
}

int csupperbound_cond_signal(csupperbound_cond_t * cond)
{
#if COND_VAR
    return REAL(pthread_cond_signal) (cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int csupperbound_cond_broadcast(csupperbound_cond_t * cond)
{
#if COND_VAR
    return REAL(pthread_cond_broadcast) (cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int csupperbound_cond_destroy(csupperbound_cond_t * cond)
{
#if COND_VAR
    return REAL(pthread_cond_destroy) (cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void csupperbound_thread_start(void)
{
    for (int i = 0; i < MAX_EPOCH; i++) {
        epoch[i].reorder_window = DEFAULT_REORDER;
        epoch[i].adjust_unit = DEFAULT_ADJUST_UNIT;
    }
    cur_epoch_id = -1;
}

void csupperbound_thread_exit(void)
{
}

void csupperbound_application_init(void)
{
}

void csupperbound_application_exit(void)
{
}

void csupperbound_init_context(lock_mutex_t * UNUSED(impl),
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

/* New interfaces in Libcsupperbound */
void set_ux(int is_ux)
{
    uxthread = is_ux;
}

void set_cs(int len)
{
    cri_len = len + 1;
}