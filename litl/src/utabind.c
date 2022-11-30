#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <papi.h>

#include "utabind.h"
#include "interpose.h"
#include "waiting_policy.h"
#include "utils.h"

/* Default Number */
#define NO_UX_MAX_WAIT_TIME     100000000
#define SHORT_BATCH_THRESHOLD   600000  /* Should less than 2^16 65536 */
#define ADJUST_THRESHOLD    1
#define ADJUST_FREQ     100 /* Should less than SHORT_BATCH_THRESHOLD */
#define DEFAULT_SHORT_THRESHOLD 1000
#define MAX_CS_LEN      10000000

#define S_PARKED    0
#define S_READY     1
#define S_ACTIVE    2
#define S_REQUEUE   3
#define S_PARKING   4

#define NOT_UX_THREAD 0
#define IS_UX_THREAD 1
__thread int nested_level = 0;
__thread unsigned int uxthread = NOT_UX_THREAD;
extern __thread unsigned int cur_thread_id;

__thread int cur_loc = -1;
__thread int stack_pos = -1;
#define STACK_SIZE 128
__thread int loc_stack[STACK_SIZE];

unsigned long debug_cnt_0 = 0;
unsigned long debug_cnt_1 = 0;

static inline uint32_t xor_random()
{
    static __thread uint32_t rv = 0;

    if (rv == 0)
        // rv = rand();
        rv = cur_thread_id + 100;

    uint32_t v = rv;
    v ^= v << 6;
    v ^= (uint32_t) (v) >> 21;
    v ^= v << 7;
    rv = v;

    return v;
}

static uint64_t __always_inline rdtscp(void)
{
    uint32_t a, d;
    __asm __volatile("rdtscp; mov %%eax, %0; mov %%edx, %1; cpuid":"=r"(a),
             "=r"(d)
             ::"%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t) a) | (((uint64_t) d) << 32);
}

static inline int sys_futex(int *uaddr, int op, int val,
                const struct timespec *timeout, int *uaddr2,
                int val3)
{
    return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

static inline int park_node(volatile int *var, int target, int wakens)
{
    int ret = 0;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = wakens;

    while ((ret = sys_futex((int *)var, FUTEX_WAIT_PRIVATE, NULL, &timeout, 0,
                0)) != 0) {
                    
        if (ret == -1 && errno != EINTR) {
            if (errno == EAGAIN || errno == ETIMEDOUT) {
                return errno;
            }
            perror("Unable to futex wait");
            exit(-1);
        }
    }
    return errno;
}

static inline void wake_node(utabind_mutex_t * impl, utabind_node_t *me)
{
    utabind_node_t *tmp = NULL;
    // printf("waking node %p\n", impl->pointers[me->thread_id % 8]);
    if(__atomic_compare_exchange_n(
        &impl->pointers[me->thread_id % 8], &tmp , me, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)){
        volatile int *var = &me->status;
        int ret = sys_futex((int *)var, FUTEX_WAKE_PRIVATE, 1, NULL, 0, 0);
        if (ret == -1) {
            perror("Unable to futex wake");
            exit(-1);
        }
    } else {
        tmp = __atomic_exchange_n(&impl->pointers[me->thread_id], me, __ATOMIC_RELEASE);
        if(tmp != me)
            tmp->status = S_PARKED;
        else{
            while(tmp->status == S_PARKING){
                CPU_PAUSE();
            }
            volatile int *var = &me->status;
            int ret = sys_futex((int *)var, FUTEX_WAKE_PRIVATE, 1, NULL, 0, 0);
            if (ret == -1) {
                perror("Unable to futex wake");
                exit(-1);
            }
        }
    }
}

/* Helper functions */
void *utabind_alloc_cache_align(size_t n)
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

utabind_mutex_t *utabind_mutex_create(const pthread_mutexattr_t * attr)
{
    utabind_mutex_t *impl = (utabind_mutex_t *)
        utabind_alloc_cache_align(sizeof(utabind_mutex_t));
    impl->tail = 0;
    impl->pointers = (utabind_node_t **)utabind_alloc_cache_align(sizeof(utabind_node_t*) * 8); // alloc 8 pointers to nodes
    for(int i = 0; i < 8; ++i){
        impl->pointers[i] = NULL; //initialize
    }
    return impl;
}

static int __utabind_mutex_trylock(utabind_mutex_t * impl,
                       utabind_node_t * me)
{
    utabind_node_t *expected;
    assert(me != NULL);
    me->next = NULL;
    expected = NULL;
    return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
                       __ATOMIC_ACQ_REL,
                       __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

static void waking_queue(utabind_mutex_t * impl, utabind_node_t * me)
{
    int i = 4;
    while (me && i != 0) {
        i--;
        wake_node(impl, me);
        me->status = S_ACTIVE;
        me = me->next;
    }
}

static void __utabind_mutex_unlock(utabind_mutex_t * impl,
                       utabind_node_t * me)
{
    // if(!me) printf("#################wrong#################\n");
    utabind_node_t *succ, *next = me->next, *expected, *tmp;
    // printf("core id next: %d %p\n", cur_thread_id, next);
    uint64_t spin;
    utabind_node_t *prevHead =
        (utabind_node_t *) (me->spin & 0xFFFFFFFFFFFF);
    utabind_node_t *secHead, *secTail, *cur;
    int expected_int;
    // printf("core id unlock %d, %d, %p\n", cur_thread_id % 8, cur_thread_id, me);

    int find = 0;
    tmp = me;
    // printf("core id before unlock: %d %p %p\n", cur_thread_id, impl->pointers[cur_thread_id % 8], me);
    __atomic_compare_exchange_n(
        &impl->pointers[cur_thread_id % 8], &tmp, NULL, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
    // printf("core id unlock %d, %d, %p %p\n", cur_thread_id % 8, cur_thread_id, impl->pointers[cur_thread_id % 8], me);
    if (!next) {
        // printf("core id next: %d %p\n", cur_thread_id, next);
        if (!prevHead) {
            expected = me;
            if (__atomic_compare_exchange_n
                (&impl->tail, &expected, 0, 0, __ATOMIC_RELEASE,
                 __ATOMIC_RELAXED)) {
                // printf("will out %d\n", cur_thread_id);
                goto out;
            }
        } else {
            expected = me;
            if (__atomic_compare_exchange_n
                (&impl->tail, &expected, prevHead->secTail, 0,
                 __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                wake_node(impl, prevHead);
                waking_queue(impl, prevHead->next);
                __atomic_store_n(&prevHead->spin,
                         0x1000000000000,
                         __ATOMIC_RELEASE);
                goto out;
            }
        }
        while (!(next = me->next))
            CPU_PAUSE();
    }
    succ = NULL;
    expected_int = S_ACTIVE;
    // printf("next is fucking what %p %d?\n", next, next->status);
    if (next->status == S_ACTIVE
        && __atomic_compare_exchange_n(&next->status, &expected_int,
                       S_READY, 0, __ATOMIC_ACQ_REL,
                       __ATOMIC_RELAXED)) {
        // printf("find next!\n");
        find = 1;
        cur = next;
        goto find_out;
    }

    /* Next is not active, traverse the queue */
    secHead = next;
    secTail = next;
    cur = next->next;
    while (cur) {
        expected_int = S_ACTIVE;
        if (cur->status == S_ACTIVE
            && __atomic_compare_exchange_n(&cur->status, &expected_int,
                           S_READY, 0, __ATOMIC_ACQ_REL,
                           __ATOMIC_RELAXED)) {
            if (prevHead) {
                prevHead->secTail->next = secHead;
            } else {
                prevHead = secHead;
            }
            secTail->next = NULL;
            prevHead->secTail = secTail;
            find = 1;
            break;
        }
        secTail = cur;
        cur = cur->next;
    }
 find_out:
    if (find) {
        if (prevHead && prevHead->status == S_ACTIVE) {
                // printf("have secondary!\n");
                if (prevHead->next) {
                    prevHead->next->secTail =
                        prevHead->secTail;
                }

                tmp = prevHead->next;
                prevHead->status = S_REQUEUE;
                prevHead = tmp;
            }

        spin = (uint64_t) prevHead | 0x1000000000000;   /* batch + 1 */
        /* Important: spin should not be 0 */
        /* Release barrier */
        __atomic_store_n(&cur->spin, spin, __ATOMIC_RELEASE);
        goto out;
    }

    /* Not find anything or batch */
    if (prevHead) {
        /* Secondary queue */
        prevHead->secTail->next = me->next;
        spin = 0x1000000000000; /* batch = 1 */
        /* Release barrier */
        wake_node(impl, prevHead);
        if (prevHead->next)
            waking_queue(impl, prevHead->next);
        __atomic_store_n(&prevHead->spin, spin, __ATOMIC_RELEASE);
    } else {
        succ = me->next;
        spin = 0x1000000000000; /* batch = 1 */
        /* Release barrier after */
        wake_node(impl, succ);
        if (succ->next)
            waking_queue(impl, succ->next);
        __atomic_store_n(&succ->spin, spin, __ATOMIC_RELEASE);
    }
 out:
    nested_level--;     /* Important! reduce nested level *after* release the lock */
    me->actcnt++;       /* Out of Critical Path */
}

#define DEFAULT_ACT_DURATION_TIME   1000000
#define MINIMAL_CHECK           1000
#define ALLOW_BATCH         40
#define DEFUALT_ACT_REDUCATION      (DEFAULT_ACT_DURATION_TIME/ALLOW_BATCH)

/* Using the unmodified MCS lock as the default underlying lock. */
static int __utabind_lock_ux(utabind_mutex_t * impl,
                 utabind_node_t * me)
{
    utabind_node_t *tail;
    uint64_t timestamp = rdtscp();
    uint64_t random = 0;    //xor_random() % DEFAULT_ACT_DURATION_TIME;
    uint64_t act_duration;
    int expected;
    int ret;
    utabind_node_t *tmp;
    me->thread_id = cur_thread_id;
    
requeue:
    tmp = NULL;
    if(__atomic_compare_exchange_n(
        &impl->pointers[cur_thread_id % 8], &tmp, me, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)){
        // printf("core id active %d, %d\n", cur_thread_id % 8, cur_thread_id);
        // printf("core id active status: %d %p %p\n", cur_thread_id, impl->pointers[cur_thread_id % 8], me);
        me->status = S_ACTIVE;
        me->next = NULL;
        me->spin = 0;
        tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
        // if(!tail)   printf("tail is null %d\n", cur_thread_id);
        if (tail) {
            __atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
            act_duration =
                DEFAULT_ACT_DURATION_TIME + random -
                me->actcnt * DEFUALT_ACT_REDUCATION;
            act_duration =
                act_duration < MINIMAL_CHECK ? MINIMAL_CHECK : act_duration;
            while (me->spin == 0) {
                CPU_PAUSE();
                if (me->status == S_PARKED) {
                    goto park;
                }
                // if (me->status == S_READY) {
                //     break; //get lock
                // }
                if (me->status == S_ACTIVE
                    && rdtscp() - timestamp >
                    DEFAULT_ACT_DURATION_TIME + random) {
                    expected = S_ACTIVE;
                    // printf("core id gg %d, %d\n", cur_thread_id % 8, cur_thread_id);
                    if (__atomic_compare_exchange_n(
                        &me->status, &expected, S_PARKING, 0,
                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                        /* Add to the secondary queue */
                        /* set map[cid] to null */
                        utabind_node_t *tmp = me;
                        __atomic_compare_exchange_n(
                        &impl->pointers[cur_thread_id % 8], &tmp, NULL, 0,
                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
                        me->actcnt = 0;
                        // printf("parking node %p\n", impl->pointers[cur_thread_id % 8]);
    park:
                        ret = park_node(&me->status, S_PARKED, 1000000);
                        if (ret == ETIMEDOUT || ret == EAGAIN) {
                            tmp = NULL;
                            if (__atomic_compare_exchange_n(
                                &impl->pointers[cur_thread_id % 8], &tmp, me, 0,
                                __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) 
                                me->status = S_ACTIVE;
                            else{
                                expected = S_ACTIVE;
                                if(__atomic_compare_exchange_n(
                                    &impl->pointers[cur_thread_id % 8]->status
                                    ,&expected ,S_PARKED ,0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)){
                                    __atomic_exchange_n(&impl->pointers[cur_thread_id % 8], me, __ATOMIC_RELEASE);
                                    me->status = S_ACTIVE;
                                }
                                else    goto park;
                            }
                        }
                        timestamp = rdtscp();
                    }
                    
                        
                }
                if(me->status == S_REQUEUE) {
                        // printf("goto\n");
                        goto requeue;
                    }
            }
        }
        else{
            me->status = S_READY;
        }
    }
    else {
        // printf("core id park %d, %d\n", cur_thread_id % 8, cur_thread_id);
        me->status = S_PARKED;
        me->next = NULL;
        me->spin = 0;
        tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
        if(tail){
            __atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
            goto park;
        }
        else    me->status = S_READY;
    }
    MEMORY_BARRIER();
    return 0;
}

/* Entry Point: length  */
static int __utabind_mutex_lock(utabind_mutex_t * impl,
                    utabind_node_t * me)
{
    __utabind_lock_ux(impl, me);
}

/* lock function orignal*/
int utabind_mutex_lock(utabind_mutex_t * impl, utabind_node_t * me)
{
    return __utabind_mutex_lock(impl, me);   /* Default loc 0 */
}

int utabind_mutex_trylock(utabind_mutex_t * impl,
                  utabind_node_t * me)
{

    if (!__utabind_mutex_trylock(impl, me)) {
        nested_level++; /* Per-thread nest level cnter */
#if COND_VAR
        REAL(pthread_mutex_lock)
            (&impl->posix_lock);
#endif
        return 0;
    }
    return -EBUSY;
}

/* unlock function orignal*/
void utabind_mutex_unlock(utabind_mutex_t * impl,
                  utabind_node_t * me)
{
    __utabind_mutex_unlock(impl, me);
}

int utabind_mutex_destroy(utabind_mutex_t * lock)
{
#if COND_VAR
    REAL(pthread_mutex_destroy)
        (&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int utabind_cond_init(utabind_cond_t * cond,
              const pthread_condattr_t * attr)
{
    return 0;
}

int utabind_cond_timedwait(utabind_cond_t * cond,
                   utabind_mutex_t * lock,
                   utabind_node_t * me,
                   const struct timespec *ts)
{
    return 0;
}

int utabind_cond_wait(utabind_cond_t * cond, utabind_mutex_t * lock,
              utabind_node_t * me)
{
    return utabind_cond_timedwait(cond, lock, me, 0);
}

int utabind_cond_signal(utabind_cond_t * cond)
{
    return 0;
}

void utabind_thread_start(void)
{
}

void utabind_thread_exit(void)
{
}

void utabind_application_init(void)
{
}

void utabind_application_exit(void)
{
    // printf("Dcnt 0 %lu Dcnt 1 %lu\n", debug_cnt_0, debug_cnt_1);
}

int utabind_cond_broadcast(utabind_cond_t * cond)
{
    return 0;
}

int utabind_cond_destroy(utabind_cond_t * cond)
{
    return 0;
}

void utabind_init_context(lock_mutex_t * impl,
                  lock_context_t * context, int UNUSED(number))
{
    context->actcnt = 0;
}

/* New interfaces in Libutabind */
/* set a thread is uxthread or not */
void set_ux(int is_ux)
{
    uxthread = is_ux;
}


