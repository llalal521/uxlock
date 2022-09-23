#define _GNU_SOURCE
/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Hugo Guiroux <hugo.guiroux at gmail dot com>
 *               UPMC, 2010-2011, Jean-Pierre Lozi <jean-pierre.lozi@lip6.fr>
 *                                Gaël Thomas <gael.thomas@lip6.fr>
 *                                Florian David <florian.david@lip6.fr>
 *                                Julia Lawall <julia.lawall@lip6.fr>
 *                                Gilles Muller <gilles.muller@lip6.fr>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of his software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * John M. Mellor-Crummey and Michael L. Scott. 1991.
 * Algorithms for scalable synchronization on shared-memory multiprocessors.
 * ACM Trans. Comput. Syst. 9, 1 (February 1991).
 *
 * Lock design summary:
 * The mcssteal lock is one of the most known multicore locks.
 * Its main goal is to avoid all threads spinning on the same memory address as
 * it induces contention due to the cache coherency protocol.
 * The lock is organized as a FIFO list: this ensures total fairness.
 * Each thread as its own context, which is a node that the thread will put into
 * the linked list (representing the list of threads waiting for the lock) when
 * it tries to grab the lock.
 * The lock is a linked-list composed of a pointer to the tail of the list.
 * - On lock: the thread does an atomic xchg to put itself to the end of the
 * linked list and get the previous tail of the list.
 *   If there was no other thread waiting, then the thread has the lock.
 * Otherwise, the thread spins on a memory address contained in its context.
 * - On unlock: if there is any thread, we just wake the next thread on the
 * waiting list. Otherwise we set the tail of the queue to NULL.
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <mcssteal.h>
#include <sched.h>

#include "waiting_policy.h"
#include "interpose.h"
#include "utils.h"

extern __thread unsigned int cur_thread_id;
__thread int count;
static inline void enable_stealing(mcssteal_mutex_t *impl)
{
    __atomic_store_n(&impl->no_stealing, 1, __ATOMIC_ACQ_REL);
}

static inline void disable_stealing(mcssteal_mutex_t *impl)
{
    __atomic_store_n(&impl->no_stealing, 0, __ATOMIC_ACQ_REL);
}

static inline int is_stealing_disabled(mcssteal_mutex_t *impl)
{
    return impl->no_stealing;
}

mcssteal_mutex_t *mcssteal_mutex_create(const pthread_mutexattr_t *attr) {
    mcssteal_mutex_t *impl = (mcssteal_mutex_t *)alloc_cache_align(sizeof(mcssteal_mutex_t));
    impl->tail        = 0;
    impl->no_stealing        = 1;
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, /*&errattr */ attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif

    return impl;
}

static uint64_t __always_inline rdtscp(void)
{
	uint32_t a, d;
	__asm __volatile("rdtscp; mov %%eax, %0; mov %%edx, %1; cpuid"
			 : "=r" (a), "=r" (d)
			 : : "%rax", "%rbx", "%rcx", "%rdx");
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}

static int __mcssteal_mutex_lock(mcssteal_mutex_t *impl, mcssteal_node_t *me) {
    mcssteal_node_t *tail;
    me->status = S_SPINING;
    int expected = 1;
    if (count != 100 && __atomic_compare_exchange_n(&impl->no_stealing, &expected, 0, 
                    0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        me->status = S_FAST;
        count++;
        // printf("lock %d fast return\n", cur_thread_id);
        return 0;
    }

    assert(me != NULL);
    me->next = LOCKED;
    tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
    /* Should preseve the order between set next and set tail */
    if (!tail) {
        while(1) {
            CPU_PAUSE();
            expected = 1;
           if(__atomic_compare_exchange_n(&impl->no_stealing, &expected, 0, 
                    0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
                break;
        }
        // printf("lock %d here return\n", cur_thread_id);
        goto succ;
    } 
    // if (is_stealing_disabled(impl))
    //         disable_stealing(impl);
    me->spin = 0;
    /* Someone there, need to link in */
    __atomic_store_n(&tail->next, me, __ATOMIC_ACQ_REL);
     me->status = S_PARKING;
    
    while(me->spin == 0) {
        CPU_PAUSE();
        if(me->status == S_PARKING) {
            // printf("tid %d sleep\n", cur_thread_id);
            waiting_policy_sleep(&me->status);
            // printf("tid %d waked\n", cur_thread_id);
        }
            
    }
     /* Control Dependency */
     while(1) {
            CPU_PAUSE();
            expected = 1;
           if(__atomic_compare_exchange_n(&impl->no_stealing, &expected, 0, 
                    0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                        // printf("lock %d here1 return\n", cur_thread_id);
                        goto succ;
                    }
                
     }
succ: 
    /* Should preseve acquire semantic here */
    // printf("lock %d here1 return\n", cur_thread_id);
    MEMORY_BARRIER();
    return 0;
}

int mcssteal_mutex_lock(mcssteal_mutex_t *impl, mcssteal_node_t *me) {
    int ret = __mcssteal_mutex_lock(impl, me);
    assert(ret == 0);
#if COND_VAR
    if (ret == 0) {
        DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
        assert(REAL(pthread_mutex_lock)(&impl->posix_lock) == 0);
    }
#endif
    DEBUG("[%d] Lock acquired posix=%p\n", cur_thread_id, &impl->posix_lock);
    return ret;
}

int mcssteal_mutex_trylock(mcssteal_mutex_t *impl, mcssteal_node_t *me) {
    mcssteal_node_t *expected;
    assert(me != NULL);
    me->next = 0;
    me->spin = LOCKED;

    // The trylock is a cmp&swap, where the thread enqueue itself to the end of
    // the list only if there are nobody at the tail
    expected = NULL;
    /* No one was there - can quickly return */
    if (__atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
#if COND_VAR
        int ret = 0;
        while ((ret = REAL(pthread_mutex_trylock)(&impl->posix_lock)) == EBUSY)
            ;
        assert(ret == 0);
#endif
        return 0;
    }
    return EBUSY;
}

static void __mcssteal_mutex_unlock(mcssteal_mutex_t *impl, mcssteal_node_t *me) {
    DEBUG("[%d] Unlocking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
          impl->tail, me);
    mcssteal_node_t *expected, *next_next;
    int park = S_PARKING;
    int spining = S_SPINING;
    /* No successor yet? */
    enable_stealing(impl);
    //  printf("enable stealing %d\n", impl->no_stealing);
      if(me->status == S_FAST)
            return;
    if (!me->next) {
        // The atomic instruction is needed if a thread between the previous if
        // and now has enqueued itself at the tail
        expected = me;
        if(__atomic_compare_exchange_n(&impl->tail, &expected, 0, 0,
            __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                // printf(" stealing %d\n", impl->no_stealing);
                return;
            }
        // else {
        //     printf("failed %d\n", impl->no_stealing);
        // }
            
        /* Wait for successor to appear */
        while (!me->next && me->status != S_FAST)
            CPU_PAUSE();
       
    }
    // printf(" stealing %d 123 %d \n", impl->no_stealing, me->status);
    // if(me->next) {
    //     printf("has next %d\n", me->next->status);
    // }
    // else {
    //     return;
    // }
    MEMORY_BARRIER();
    if(__atomic_compare_exchange_n(&me->next->status, &park, &spining, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        waiting_policy_wake(&me->next->status);
    me->next->spin = 1;
    next_next = me->next->next;
    park = S_PARKING;
    if(next_next)
    if(__atomic_compare_exchange_n(&next_next->status, &park, &spining, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        waiting_policy_wake(&next_next->status);
    // printf(" stealing %d\n", impl->no_stealing);
}

void mcssteal_mutex_unlock(mcssteal_mutex_t *impl, mcssteal_node_t *me) {
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif
    __mcssteal_mutex_unlock(impl, me);
}

int mcssteal_mutex_destroy(mcssteal_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int mcssteal_cond_init(mcssteal_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcssteal_cond_timedwait(mcssteal_cond_t *cond, mcssteal_mutex_t *lock, mcssteal_node_t *me,
                       const struct timespec *ts) {
#if COND_VAR
    int res;

    __mcssteal_mutex_unlock(lock, me);
    DEBUG("[%d] Sleep cond=%p lock=%p posix_lock=%p\n", cur_thread_id, cond,
          lock, &(lock->posix_lock));
    DEBUG_PTHREAD("[%d] Cond posix = %p lock = %p\n", cur_thread_id, cond,
                  &lock->posix_lock);

    if (ts)
        res = REAL(pthread_cond_timedwait)(cond, &lock->posix_lock, ts);
    else
        res = REAL(pthread_cond_wait)(cond, &lock->posix_lock);

    if (res != 0 && res != ETIMEDOUT) {
        fprintf(stderr, "Error on cond_{timed,}wait %d\n", res);
        assert(0);
    }

    int ret = 0;
    if ((ret = REAL(pthread_mutex_unlock)(&lock->posix_lock)) != 0) {
        fprintf(stderr, "Error on mutex_unlock %d\n", ret == EPERM);
        assert(0);
    }

    mcssteal_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcssteal_cond_wait(mcssteal_cond_t *cond, mcssteal_mutex_t *lock, mcssteal_node_t *me) {
    return mcssteal_cond_timedwait(cond, lock, me, 0);
}

int mcssteal_cond_signal(mcssteal_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcssteal_cond_broadcast(mcssteal_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcssteal_cond_destroy(mcssteal_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void mcssteal_thread_start(void) {
}

void mcssteal_thread_exit(void) {
}

void mcssteal_application_init(void) {
}

void mcssteal_application_exit(void) {
}
void mcssteal_init_context(lock_mutex_t *UNUSED(impl),
                      lock_context_t *UNUSED(context), int UNUSED(number)) {
}
