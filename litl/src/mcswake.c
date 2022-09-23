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
 * The mcswake lock is one of the most known multicore locks.
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
#include <mcswake.h>
#include <sched.h>

#include "waiting_policy.h"
#include "interpose.h"
#include "utils.h"

extern __thread unsigned int cur_thread_id;

mcswake_mutex_t *mcswake_mutex_create(const pthread_mutexattr_t *attr) {
    mcswake_mutex_t *impl = (mcswake_mutex_t *)alloc_cache_align(sizeof(mcswake_mutex_t));
    impl->tail        = 0;
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, /*&errattr */ attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif

    return impl;
}

static int __mcswake_mutex_lock(mcswake_mutex_t *impl, mcswake_node_t *me) {
    mcswake_node_t *tail;

    assert(me != NULL);
    me->next = LOCKED;
    tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
    /* Should preseve the order between set next and set tail */
    if (!tail) goto succ;
    me->spin = 0;
    /* Someone there, need to link in */
    __atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
     me->status = S_PARKING;
    while(me->spin == 0) {
        CPU_PAUSE();
        if(me->status == S_PARKING) 
            waiting_policy_sleep(&me->status);
    }
     /* Control Dependency */

succ: 
    /* Should preseve acquire semantic here */
    MEMORY_BARRIER();
    return 0;
}

int mcswake_mutex_lock(mcswake_mutex_t *impl, mcswake_node_t *me) {
    int ret = __mcswake_mutex_lock(impl, me);
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

int mcswake_mutex_trylock(mcswake_mutex_t *impl, mcswake_node_t *me) {
    mcswake_node_t *expected;
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

static void __mcswake_mutex_unlock(mcswake_mutex_t *impl, mcswake_node_t *me) {
    DEBUG("[%d] Unlocking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
          impl->tail, me);
    mcswake_node_t *expected, *next_next;
    int park = S_PARKING;
    int spining = S_SPINING;
    /* No successor yet? */
    if (!me->next) {
        // The atomic instruction is needed if a thread between the previous if
        // and now has enqueued itself at the tail
        expected = me;
        if(__atomic_compare_exchange_n(&impl->tail, &expected, 0, 0,
            __ATOMIC_RELEASE, __ATOMIC_RELAXED))
            return;
        /* Wait for successor to appear */
        while (!me->next)
            CPU_PAUSE();
    }
    MEMORY_BARRIER();
    if(__atomic_compare_exchange_n(&me->next->status, &park, &spining, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        waiting_policy_wake(&me->next->status);
    me->next->spin = 1;
    next_next = me->next->next;
    if(next_next)
    if(__atomic_compare_exchange_n(&next_next->status, &park, &spining, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        waiting_policy_wake(&next_next->status);
}

void mcswake_mutex_unlock(mcswake_mutex_t *impl, mcswake_node_t *me) {
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif
    __mcswake_mutex_unlock(impl, me);
}

int mcswake_mutex_destroy(mcswake_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int mcswake_cond_init(mcswake_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcswake_cond_timedwait(mcswake_cond_t *cond, mcswake_mutex_t *lock, mcswake_node_t *me,
                       const struct timespec *ts) {
#if COND_VAR
    int res;

    __mcswake_mutex_unlock(lock, me);
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

    mcswake_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcswake_cond_wait(mcswake_cond_t *cond, mcswake_mutex_t *lock, mcswake_node_t *me) {
    return mcswake_cond_timedwait(cond, lock, me, 0);
}

int mcswake_cond_signal(mcswake_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcswake_cond_broadcast(mcswake_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcswake_cond_destroy(mcswake_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void mcswake_thread_start(void) {
}

void mcswake_thread_exit(void) {
}

void mcswake_application_init(void) {
}

void mcswake_application_exit(void) {
}
void mcswake_init_context(lock_mutex_t *UNUSED(impl),
                      lock_context_t *UNUSED(context), int UNUSED(number)) {
}
