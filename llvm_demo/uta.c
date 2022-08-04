#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include "uta.h"
#include "utils.h"
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <papi.h>

/* Default Number */
#define MAX_REORDER 10000000000
#define REORDER_THRESHOLD 10000000000

#define NOT_UX_THREAD 0
#define IS_UX_THREAD 1
__thread unsigned int uxthread = NOT_UX_THREAD;
__thread unsigned int cri_len;
__thread int nested_level = 0;
__thread int stack_pos = -1;
#define MAX_EPOCH 128
__thread int loc_cri[MAX_EPOCH];
/* Predict CS by location */
#define MAX_LOC 128
__thread long long tt_start[MAX_LOC], tt_end[MAX_LOC], critical_len[MAX_LOC];
int cnt[MAX_LOC] = {0};

/* Per-thread private stack, avoid nest lock cover loc_cri*/
int push_loc(int epoch_id)
{
    stack_pos++;
    if (stack_pos == MAX_EPOCH)
        return -ENOSPC;
    loc_cri[stack_pos] = epoch_id;
    return 0;
}

/* Per-thread private stack */
int pop_loc(void)
{
    if (stack_pos < 0)
        return -EINVAL;
    return loc_cri[stack_pos--];
}

/* Helper functions */
void *uta_alloc_cache_align(size_t n)
{
    void *res = 0;
    if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res)
    {
        fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
                (unsigned long long)cache_align(n));
        exit(-1);
    }
    return res;
}

lock_transparent_mutex_t *lock_create(const pthread_mutexattr_t *attr)
{
    lock_transparent_mutex_t *impl = alloc_cache_align(sizeof *impl);
    impl->lock_lock = lock_mutex_create(attr);
    return impl;
}

uint64_t get_current_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000000LL + now.tv_nsec;
}

uta_mutex_t *uta_mutex_create(const pthread_mutexattr_t *attr)
{
    uta_mutex_t *impl =
        (uta_mutex_t *)uta_alloc_cache_align(sizeof(uta_mutex_t));
    impl->tail = 0;
    impl->batch = 0;
    impl->adjust = 0;
    impl->threshold = 1200;
    return impl;
}

static int __uta_mutex_trylock(uta_mutex_t *impl, uta_node_t *me)
{
    uta_node_t *expected;
    assert(me != NULL);
    me->next = NULL;
    expected = NULL;
    return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
                                       __ATOMIC_ACQ_REL,
                                       __ATOMIC_RELAXED)
               ? 0
               : -EBUSY;
}

/* Find short CS according to the threshold */
static uta_node_t *find_successor(uta_mutex_t *impl, uta_node_t *me)
{
    uta_node_t *next = me->next;
    int find = 0;
    uint32_t batch = impl->batch;
    int32_t adjust = impl->adjust;
    int32_t threshold = impl->threshold;
    uta_node_t *secHead, *secTail, *cur, *minTail, *minNode;
    if (next->cri_len < impl->threshold)
    {
        find = 1;
        cur = next;
        goto is_find;
    }

    secHead = next;
    secTail = next;
    cur = next->next;
    while (cur)
    {
        if (cur->cri_len < impl->threshold)
        {
            if (me->spin > 1)
                ((uta_node_t *)(me->spin))->secTail->next =
                    secHead;
            else
                me->spin = (uintptr_t)secHead;
            secTail->next = NULL;
            ((uta_node_t *)(me->spin))->secTail = secTail;
            find = 1;
            break;
        }
        secTail = cur;
        cur = cur->next;
    }
    // printf("threshold %d\n",  impl->threshold);
is_find:
    if(batch >= REORDER_THRESHOLD)
        impl->adjust = adjust > 0 ? 0 : adjust - 1;
    if (find)
    {
        if (impl->adjust < -10)
            if (threshold > 1000 && threshold <= 2000)
                impl->threshold = threshold - 200;
            else if (threshold > 2000 && threshold <= 5000)
                impl->threshold = threshold - 400;
            else if (threshold > 5000)
                impl->threshold = threshold - 1000;
        return cur;
    }
    else{
        impl->adjust = adjust > 0 ? 0 : adjust + 1;
        if (impl->adjust > 1)
        {
            if (threshold < 3000)
                impl->threshold = threshold + 500;
            else
                impl->threshold = threshold + 1000;
        }
    } 
    return NULL;
}

static void __uta_mutex_unlock(uta_mutex_t *impl, uta_node_t *me)
{
    uta_node_t *succ;

    nested_level--;
    if (!me->next)
    {
        if (me->spin <= 1)
        {
            if (__sync_val_compare_and_swap(&impl->tail, me, NULL) == me)
            {
                impl->batch = 0;
                return;
            }
        }
        else
        {
            uta_node_t *secHead = (uta_node_t *)me->spin;
            if (__sync_val_compare_and_swap(&impl->tail, me, secHead->secTail) == me)
            {
                impl->batch = 0;
                secHead->spin = 1;
                return;
            }
        }

        while (!me->next)
            CPU_PAUSE();
    }
    /*
     * Determine the next lock holder and pass the lock by
     * setting its spin field
     */
    // MEMORY_BARRIER();
    // me->next->spin = 1;
    succ = NULL;
    if (impl->batch < REORDER_THRESHOLD && (succ = find_successor(impl, me)))
    {
        succ->spin = me->spin ? me->spin : 1;
        impl->batch++;
    }
    else if (me->spin > 1)
    {
        impl->batch = 0;
        succ = (uta_node_t *)me->spin;
        succ->secTail->next = me->next;
        succ->spin = 1;
    }
    else
    {
        me->next->spin = 1;
    }

    if (impl->adjust < -10)
        if (impl->threshold > 1000 && impl->threshold <= 2000)
            impl->threshold = impl->threshold - 200;
        else if (impl->threshold > 2000 && impl->threshold <= 5000)
            impl->threshold = impl->threshold - 400;
        else if (impl->threshold > 5000)
            impl->threshold = impl->threshold - 1000;
    if (impl->adjust > 1)
    {
        if (impl->threshold < 3000)
            impl->threshold = impl->threshold + 500;
        else
            impl->threshold = impl->threshold + 1000;
    }
}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __uta_lock_fifo(uta_mutex_t *impl, uta_node_t *me)
{
    uta_node_t *tail;
    me->next = NULL;
    me->spin = 0;
    tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
    if (tail)
    {
        __atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
        while (me->spin == 0)
            CPU_PAUSE();
    }
    MEMORY_BARRIER();
    return 0;
}

/* not-ux-thread reorder if queue not empty */
static inline int __uta_lock_reorder(uta_mutex_t *impl, uta_node_t *me,
                                     uint64_t reorder_window)
{
    uint64_t reorder_window_ddl;
    uint64_t current_ns;
    uint32_t cnt = 0, next_check = 100;
    /* Fast Path */
    if (reorder_window < REORDER_THRESHOLD || impl->tail == NULL)
    {
        return __uta_lock_fifo(impl, me);
    }
    /* Someone is holding the lock */
    reorder_window_ddl = get_current_ns() + reorder_window;
    while ((current_ns = get_current_ns()) < reorder_window_ddl)
    {
        // if (cnt++ == next_check)
        // {
        /* Check the queue in a backoff manner */
        if (impl->tail == NULL)
        {
            break;
        }
        //         next_check <<= 1;
        // }
    }
    return __uta_lock_fifo(impl, me);
}

static inline int __uta_lock_eventually(uta_mutex_t *impl, uta_node_t *me)
{
    return __uta_lock_reorder(impl, me, MAX_REORDER);
}

static int __uta_mutex_lock(uta_mutex_t *impl, uta_node_t *me)
{
    int ret;
    nested_level++;
    if (nested_level > 1)
    {
        return __uta_lock_fifo(impl, me);
    }
    if (uxthread)
    {
        return __uta_lock_fifo(impl, me);
    }
    else
    {
        return __uta_lock_eventually(impl, me);
    }
}

/* lock function  with perdict critical*/
int uta_mutex_lock_cri(uta_mutex_t *impl, uta_node_t *me, int loc)
{
    me->cri_len = critical_len[loc];
    // if (nested_level > 0)
    //         me->cri_len = 0;
    int ret = __uta_mutex_lock(impl, me);
    set_starttime(loc);
    return ret;
}

/* lock function orignal*/
int uta_mutex_lock(uta_mutex_t *impl, uta_node_t *me)
{
    me->cri_len = cri_len;
    int ret = __uta_mutex_lock(impl, me);

    return ret;
}

int uta_mutex_trylock(uta_mutex_t *impl, uta_node_t *me)
{
    if (!__uta_mutex_trylock(impl, me))
    {
#if COND_VAR
        REAL(pthread_mutex_lock)
        (&impl->posix_lock);
#endif
        return 0;
    }
    return -EBUSY;
}

/* unlock function orignal*/
void uta_mutex_unlock(uta_mutex_t *impl, uta_node_t *me)
{
    __uta_mutex_unlock(impl, me);
}

/* unlock function with perdict critical */
void uta_mutex_unlock_cri(uta_mutex_t *impl, uta_node_t *me)
{
    set_endtime();
    __uta_mutex_unlock(impl, me);
}

int uta_mutex_destroy(uta_mutex_t *lock)
{
#if COND_VAR
    REAL(pthread_mutex_destroy)
    (&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int uta_cond_init(uta_cond_t *cond, const pthread_condattr_t *attr)
{
    return 0;
}

int uta_cond_timedwait(uta_cond_t *cond, uta_mutex_t *lock,
                       uta_node_t *me, const struct timespec *ts)
{
    return 0;
}

int uta_cond_wait(uta_cond_t *cond, uta_mutex_t *lock, uta_node_t *me)
{
    return uta_cond_timedwait(cond, lock, me, 0);
}

int uta_cond_signal(uta_cond_t *cond)
{
    return 0;
}

int uta_cond_broadcast(uta_cond_t *cond)
{
    return 0;
}

int uta_cond_destroy(uta_cond_t *cond)
{
    return 0;
}

/* New interfaces in Libuta */
/* get time before entry critical */
void set_starttime(int loc)
{
    tt_start[loc] = PAPI_get_real_cyc();
    push_loc(loc);
}

/* get time after entry critical and perdict next time critical len*/
void set_endtime()
{

    int loc, duration;
    loc = pop_loc();
    tt_end[loc] = PAPI_get_real_cyc();
    duration = tt_end[loc] - tt_start[loc];

    if (critical_len[loc] == 0)
    {
        critical_len[loc] = duration;
    }
    else if (duration > 10000 && critical_len[loc] < 1000 && critical_len[loc] > 0)
    {
        critical_len[loc] = critical_len[loc] * 2;
        return;
    }
    else
        critical_len[loc] = critical_len[loc] * 3 / 4 + duration / 4;
}

/* set thread critical len (used by orignal) */
void set_cs(int len)
{
    cri_len = len + 1;
}

/* set a thread is uxthread or not */
void set_ux(int is_ux)
{
    uxthread = is_ux;
}
