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
 */
#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include "padding.h"
#define LOCK_ALGORITHM "SPINLOCK"
#define NEED_CONTEXT 0
#define SUPPORT_WAITING 1

typedef struct spinlock_mutex {
#if COND_VAR
    pthread_mutex_t posix_lock;
    char __pad[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
    volatile int spin_lock __attribute__((aligned(L_CACHE_LINE_SIZE)));
} spinlock_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t spinlock_cond_t;
typedef void *spinlock_context_t; // Unused, take the less space as possible

spinlock_mutex_t *spinlock_mutex_create(const pthread_mutexattr_t *attr);
int spinlock_mutex_lock(spinlock_mutex_t *impl, spinlock_context_t *me);
int spinlock_mutex_trylock(spinlock_mutex_t *impl, spinlock_context_t *me);
void spinlock_mutex_unlock(spinlock_mutex_t *impl, spinlock_context_t *me);
int spinlock_mutex_destroy(spinlock_mutex_t *lock);
int spinlock_cond_init(spinlock_cond_t *cond, const pthread_condattr_t *attr);
int spinlock_cond_timedwait(spinlock_cond_t *cond, spinlock_mutex_t *lock,
                            spinlock_context_t *me, const struct timespec *ts);
int spinlock_cond_wait(spinlock_cond_t *cond, spinlock_mutex_t *lock,
                       spinlock_context_t *me);
int spinlock_cond_signal(spinlock_cond_t *cond);
int spinlock_cond_broadcast(spinlock_cond_t *cond);
int spinlock_cond_destroy(spinlock_cond_t *cond);
void spinlock_thread_start(void);
void spinlock_thread_exit(void);
void spinlock_application_init(void);
void spinlock_application_exit(void);

typedef spinlock_mutex_t lock_mutex_t;
typedef spinlock_context_t lock_context_t;
typedef spinlock_cond_t lock_cond_t;

#define lock_mutex_create spinlock_mutex_create
#define lock_mutex_lock spinlock_mutex_lock
#define lock_mutex_trylock spinlock_mutex_trylock
#define lock_mutex_unlock spinlock_mutex_unlock
#define lock_mutex_destroy spinlock_mutex_destroy
#define lock_cond_init spinlock_cond_init
#define lock_cond_timedwait spinlock_cond_timedwait
#define lock_cond_wait spinlock_cond_wait
#define lock_cond_signal spinlock_cond_signal
#define lock_cond_broadcast spinlock_cond_broadcast
#define lock_cond_destroy spinlock_cond_destroy
#define lock_thread_start spinlock_thread_start
#define lock_thread_exit spinlock_thread_exit
#define lock_application_init spinlock_application_init
#define lock_application_exit spinlock_application_exit
#define lock_init_context spinlock_init_context

#endif // __SPINLOCK_H__
