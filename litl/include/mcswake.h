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
#ifndef __mcswake_H__
#define __mcswake_H__

#include "padding.h"
#define LOCK_ALGORITHM "MCSWAKE"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1

#define S_SPINING 1
#define S_PARKING 0
typedef struct mcswake_node {
    struct mcswake_node *volatile next;
    char __pad[pad_to_cache_line(sizeof(struct mcswake_node *))];
    int status;
    char __pad1[pad_to_cache_line(sizeof(int))];
    volatile int spin __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcswake_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct mcswake_mutex {
#if COND_VAR
    pthread_mutex_t posix_lock;
    char __pad[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
    struct mcswake_node *volatile tail __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcswake_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t mcswake_cond_t;
mcswake_mutex_t *mcswake_mutex_create(const pthread_mutexattr_t *attr);
int mcswake_mutex_lock(mcswake_mutex_t *impl, mcswake_node_t *me);
int mcswake_mutex_trylock(mcswake_mutex_t *impl, mcswake_node_t *me);
void mcswake_mutex_unlock(mcswake_mutex_t *impl, mcswake_node_t *me);
int mcswake_mutex_destroy(mcswake_mutex_t *lock);
int mcswake_cond_init(mcswake_cond_t *cond, const pthread_condattr_t *attr);
int mcswake_cond_timedwait(mcswake_cond_t *cond, mcswake_mutex_t *lock, mcswake_node_t *me,
                       const struct timespec *ts);
int mcswake_cond_wait(mcswake_cond_t *cond, mcswake_mutex_t *lock, mcswake_node_t *me);
int mcswake_cond_signal(mcswake_cond_t *cond);
int mcswake_cond_broadcast(mcswake_cond_t *cond);
int mcswake_cond_destroy(mcswake_cond_t *cond);
void mcswake_thread_start(void);
void mcswake_thread_exit(void);
void mcswake_application_init(void);
void mcswake_application_exit(void);
void mcswake_init_context(mcswake_mutex_t *impl, mcswake_node_t *context, int number);

typedef mcswake_mutex_t lock_mutex_t;
typedef mcswake_node_t lock_context_t;
typedef mcswake_cond_t lock_cond_t;

#define lock_mutex_create mcswake_mutex_create
#define lock_mutex_lock mcswake_mutex_lock
#define lock_mutex_trylock mcswake_mutex_trylock
#define lock_mutex_unlock mcswake_mutex_unlock
#define lock_mutex_destroy mcswake_mutex_destroy
#define lock_cond_init mcswake_cond_init
#define lock_cond_timedwait mcswake_cond_timedwait
#define lock_cond_wait mcswake_cond_wait
#define lock_cond_signal mcswake_cond_signal
#define lock_cond_broadcast mcswake_cond_broadcast
#define lock_cond_destroy mcswake_cond_destroy
#define lock_thread_start mcswake_thread_start
#define lock_thread_exit mcswake_thread_exit
#define lock_application_init mcswake_application_init
#define lock_application_exit mcswake_application_exit
#define lock_init_context mcswake_init_context

#endif // __mcswake_H__
