#ifndef __uxpick_H__
#define __uxpick_H__

#include "padding.h"
#define LOCK_ALGORITHM "UXPICK"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1
#define uxpick_THRESHOLD 100

typedef struct uxpick_node {
    struct uxpick_node *volatile next;
    // char __pad[pad_to_cache_line(sizeof(struct uxpick_node *))];
    struct uxpick_node *volatile secTail;
    // char __pad2[pad_to_cache_line(sizeof(struct uxpick_node *))];
    // int socket;
    int uxthread;
    volatile uintptr_t spin __attribute__((aligned(L_CACHE_LINE_SIZE)));
} uxpick_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct uxpick_mutex {
#if COND_VAR
    pthread_mutex_t posix_lock;
    char __pad[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
    struct uxpick_node *volatile tail;
    int batch;
} uxpick_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t uxpick_cond_t;
uxpick_mutex_t *uxpick_mutex_create(const pthread_mutexattr_t *attr);
int uxpick_mutex_lock(uxpick_mutex_t *impl, uxpick_node_t *me);
int uxpick_mutex_trylock(uxpick_mutex_t *impl, uxpick_node_t *me);
void uxpick_mutex_unlock(uxpick_mutex_t *impl, uxpick_node_t *me);
int uxpick_mutex_destroy(uxpick_mutex_t *lock);
int uxpick_cond_init(uxpick_cond_t *cond, const pthread_condattr_t *attr);
int uxpick_cond_timedwait(uxpick_cond_t *cond, uxpick_mutex_t *lock, uxpick_node_t *me,
                       const struct timespec *ts);
int uxpick_cond_wait(uxpick_cond_t *cond, uxpick_mutex_t *lock, uxpick_node_t *me);
int uxpick_cond_signal(uxpick_cond_t *cond);
int uxpick_cond_broadcast(uxpick_cond_t *cond);
int uxpick_cond_destroy(uxpick_cond_t *cond);
void uxpick_thread_start(void);
void uxpick_thread_exit(void);
void uxpick_application_init(void);
void uxpick_application_exit(void);
void uxpick_init_context(uxpick_mutex_t *impl, uxpick_node_t *context, int number);

typedef uxpick_mutex_t lock_mutex_t;
typedef uxpick_node_t lock_context_t;
typedef uxpick_cond_t lock_cond_t;

#define lock_mutex_create uxpick_mutex_create
#define lock_mutex_lock uxpick_mutex_lock
#define lock_mutex_trylock uxpick_mutex_trylock
#define lock_mutex_unlock uxpick_mutex_unlock
#define lock_mutex_destroy uxpick_mutex_destroy
#define lock_cond_init uxpick_cond_init
#define lock_cond_timedwait uxpick_cond_timedwait
#define lock_cond_wait uxpick_cond_wait
#define lock_cond_signal uxpick_cond_signal
#define lock_cond_broadcast uxpick_cond_broadcast
#define lock_cond_destroy uxpick_cond_destroy
#define lock_thread_start uxpick_thread_start
#define lock_thread_exit uxpick_thread_exit
#define lock_application_init uxpick_application_init
#define lock_application_exit uxpick_application_exit
#define lock_init_context uxpick_init_context

#endif // __uxpick_H__
