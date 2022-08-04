/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Hugo Guiroux <hugo.guiroux at gmail dot com>
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
 * Michael L. Scott. 2013.
 * Shared-Memory Synchronization.
 * Morgan & Claypool Publishers.
 *
 * Lock design summary:
 * This is just a test and set on the same memory location.
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <spinlock.h>

#include "waiting_policy.h"
#include "interpose.h"
#include "utils.h"

extern __thread unsigned int cur_thread_id;
__thread int nested_level = 0;


spinlock_mutex_t *spinlock_mutex_create(const pthread_mutexattr_t *attr) {
    spinlock_mutex_t *impl =
        (spinlock_mutex_t *)alloc_cache_align(sizeof(spinlock_mutex_t));
    impl->spin_lock = UNLOCKED;
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, attr);
#endif

    return impl;
}

void stub() {
    // printf("Nested!\n");
}

int spinlock_mutex_lock(spinlock_mutex_t *impl,
                        spinlock_context_t *UNUSED(me)) {
    int expected = UNLOCKED;

    nested_level ++;
    // if (nested_level > 1) {
    //     stub();
    // }

    while (!__atomic_compare_exchange_n(&impl->spin_lock, &expected, LOCKED, 0,
        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        expected = UNLOCKED;
        waiting_policy_sleep((volatile int *)&impl->spin_lock);
    }
#if COND_VAR
    int ret = REAL(pthread_mutex_lock)(&impl->posix_lock);
    assert(ret == 0);
#endif
    return 0;
}

int spinlock_mutex_trylock(spinlock_mutex_t *impl,
                           spinlock_context_t *UNUSED(me)) {
    int expected = UNLOCKED;
    if (__atomic_compare_exchange_n(&impl->spin_lock, &expected, LOCKED, 0,
        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
#if COND_VAR
        int ret = 0;
        while ((ret = REAL(pthread_mutex_trylock)(&impl->posix_lock)) == EBUSY)
            CPU_PAUSE();
        assert(ret == 0);
#endif
        nested_level ++;
        return 0;
    }

    return EBUSY;
}

void __spinlock_mutex_unlock(spinlock_mutex_t *impl) {
    MEMORY_BARRIER();
    nested_level --;
    waiting_policy_wake((volatile int *)&impl->spin_lock);
}

void spinlock_mutex_unlock(spinlock_mutex_t *impl,
                           spinlock_context_t *UNUSED(me)) {
#if COND_VAR
    int ret = REAL(pthread_mutex_unlock)(&impl->posix_lock);
    assert(ret == 0);
#endif
    __spinlock_mutex_unlock(impl);
}

int spinlock_mutex_destroy(spinlock_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int spinlock_cond_init(spinlock_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int spinlock_cond_timedwait(spinlock_cond_t *cond, spinlock_mutex_t *lock,
                            spinlock_context_t *me, const struct timespec *ts) {
#if COND_VAR
    int res;

    __spinlock_mutex_unlock(lock);

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

    spinlock_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int spinlock_cond_wait(spinlock_cond_t *cond, spinlock_mutex_t *lock,
                       spinlock_context_t *me) {
    return spinlock_cond_timedwait(cond, lock, me, 0);
}

int spinlock_cond_signal(spinlock_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int spinlock_cond_broadcast(spinlock_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int spinlock_cond_destroy(spinlock_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void spinlock_thread_start(void) {
}

void spinlock_thread_exit(void) {
}

void spinlock_application_init(void) {
}

void spinlock_application_exit(void) {
}

void spinlock_init_context(lock_mutex_t *UNUSED(impl),
                           lock_context_t *UNUSED(context),
                           int UNUSED(number)) {
}
