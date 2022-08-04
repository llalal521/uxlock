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
 * The uxpick lock is one of the most known multicore locks.
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
#include <uxpick.h>

#include "waiting_policy.h"
#include "interpose.h"
#include "utils.h"

extern __thread unsigned int cur_thread_id;
__thread unsigned int uxthread;

uxpick_mutex_t *uxpick_mutex_create(const pthread_mutexattr_t *attr) {
    uxpick_mutex_t *impl = (uxpick_mutex_t *)alloc_cache_align(sizeof(uxpick_mutex_t));
    impl->tail        = 0;
    impl->batch = 0;
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, /*&errattr */ attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif

    return impl;
}

static int __uxpick_mutex_lock(uxpick_mutex_t *impl, uxpick_node_t *me) {
    uxpick_node_t *tail;

    assert(me != NULL);

    me->next = LOCKED;
    me->spin = 0;
    me->uxthread = uxthread;
    // me->socket = -1;

    // The atomic instruction is needed when two threads try to put themselves
    // at the tail of the list at the same time
    tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);

    /* No one there? */
    if (!tail) {
        return 0;
    }

    /* Someone there, need to link in */
    // me->socket = current_numa_node();
    tail->next = me;
    COMPILER_BARRIER();

    waiting_policy_sleep((volatile int *)&me->spin);

    DEBUG("[%d] (2) Locking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
          impl->tail, me);
    return 0;
}

int uxpick_mutex_lock(uxpick_mutex_t *impl, uxpick_node_t *me) {
    int ret = __uxpick_mutex_lock(impl, me);
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

int uxpick_mutex_trylock(uxpick_mutex_t *impl, uxpick_node_t *me) {
    uxpick_node_t *tail;

    assert(me != NULL);

    me->next = 0;
    me->spin = LOCKED;

    // The trylock is a cmp&swap, where the thread enqueue itself to the end of
    // the list only if there are nobody at the tail
    tail = __sync_val_compare_and_swap(&impl->tail, 0, me);

    /* No one was there - can quickly return */
    if (!tail) {
        DEBUG("[%d] TryLocking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
              impl->tail, me);
#if COND_VAR
        DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
        int ret = 0;
        while ((ret = REAL(pthread_mutex_trylock)(&impl->posix_lock)) == EBUSY)
            ;
        assert(ret == 0);
#endif
        return 0;
    }

    return EBUSY;
}

static uxpick_node_t *find_successor(uxpick_node_t *me)
{
    uxpick_node_t *next = me->next;
    uxpick_node_t *secHead, *secTail, *cur;
    // int my_socket = me->socket;

    // if (my_socket == -1)
    //     my_socket = current_numa_node();

    if (next->uxthread)
        return next;

    secHead = next;
    secTail = next;
    cur = next->next;

    while (cur) {
        if (cur->uxthread) {
            if (me->spin > 1)
                ((uxpick_node_t *)(me->spin))->secTail->next = secHead;
            else
                me->spin = (uintptr_t)secHead;
            secTail->next = NULL;
            ((uxpick_node_t *)(me->spin))->secTail = secTail;
            return cur;
        }
        secTail = cur;
        cur = cur->next;
    }
    return NULL;
}

void set_ux(int is_ux)
{
    uxthread = is_ux;
}



#if 0
#define MAX_RECORD 1000
int length[MAX_RECORD] = {0};
int record_cnt = 0;
#endif
static void __uxpick_mutex_unlock(uxpick_mutex_t *impl, uxpick_node_t *me) {
    uxpick_node_t *succ;

    DEBUG("[%d] Unlocking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
          impl->tail, me);

    if (!me->next) {
        if (me->spin <= 1) {
            if (__sync_val_compare_and_swap(&impl->tail, me, NULL) == me) {
                impl->batch = 0;
                return;
            }
        } else {
            uxpick_node_t *secHead = (uxpick_node_t *)me->spin;
            if (__sync_val_compare_and_swap(&impl->tail, me, secHead->secTail) == me) {
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
    succ = NULL;

    if (impl->batch < uxpick_THRESHOLD && (succ = find_successor(me))) {
        succ->spin = me->spin?me->spin:1;
        impl->batch ++;
    } else if (me->spin > 1) {
        impl->batch = 0;
        succ = (uxpick_node_t *)me->spin;
        succ->secTail->next = me->next;
        succ->spin = 1;
    } else {
        me->next->spin = 1;
    }

}

void uxpick_mutex_unlock(uxpick_mutex_t *impl, uxpick_node_t *me) {
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif
    __uxpick_mutex_unlock(impl, me);
}

int uxpick_mutex_destroy(uxpick_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int uxpick_cond_init(uxpick_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int uxpick_cond_timedwait(uxpick_cond_t *cond, uxpick_mutex_t *lock, uxpick_node_t *me,
                       const struct timespec *ts) {
#if COND_VAR
    int res;

    __uxpick_mutex_unlock(lock, me);
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

    uxpick_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int uxpick_cond_wait(uxpick_cond_t *cond, uxpick_mutex_t *lock, uxpick_node_t *me) {
    return uxpick_cond_timedwait(cond, lock, me, 0);
}

int uxpick_cond_signal(uxpick_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int uxpick_cond_broadcast(uxpick_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int uxpick_cond_destroy(uxpick_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void uxpick_thread_start(void) {
}

void uxpick_thread_exit(void) {
}

void uxpick_application_init(void) {
}

void uxpick_application_exit(void) {
#if 0
    uint64_t tot = 0;
	int cnt = 0;
    for (int i = 0; i < MAX_RECORD; i ++) {
		if (length[i] == 0)
            break;
        tot += length[i];
        cnt ++;
	}
	printf("avg len %lf\n", (double)tot/cnt);
#endif
}

void uxpick_init_context(lock_mutex_t *UNUSED(impl),
                      lock_context_t *UNUSED(context), int UNUSED(number)) {
}
