#ifndef __cspick_H__
#define __cspick_H__

#include "padding.h"
#define LOCK_ALGORITHM "CSPICK"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1
#define cspick_THRESHOLD 50

typedef struct cspick_node {
    struct cspick_node *volatile next;
    // char __pad[pad_to_cache_line(sizeof(struct cspick_node *))];
    struct cspick_node *volatile secTail;
    // char __pad2[pad_to_cache_line(sizeof(struct cspick_node *))];
    // int socket;
    int uxthread;
    int cri_len;
    volatile uintptr_t spin __attribute__((aligned(L_CACHE_LINE_SIZE)));
} cspick_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct cspick_mutex {
#if COND_VAR
    pthread_mutex_t posix_lock;
    char __pad[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
    struct cspick_node *volatile tail;
    int batch;
} cspick_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t cspick_cond_t;
cspick_mutex_t *cspick_mutex_create(const pthread_mutexattr_t *attr);
int cspick_mutex_lock(cspick_mutex_t *impl, cspick_node_t *me);
int cspick_mutex_trylock(cspick_mutex_t *impl, cspick_node_t *me);
void cspick_mutex_unlock(cspick_mutex_t *impl, cspick_node_t *me);
int cspick_mutex_destroy(cspick_mutex_t *lock);
int cspick_cond_init(cspick_cond_t *cond, const pthread_condattr_t *attr);
int cspick_cond_timedwait(cspick_cond_t *cond, cspick_mutex_t *lock, cspick_node_t *me,
                       const struct timespec *ts);
int cspick_cond_wait(cspick_cond_t *cond, cspick_mutex_t *lock, cspick_node_t *me);
int cspick_cond_signal(cspick_cond_t *cond);
int cspick_cond_broadcast(cspick_cond_t *cond);
int cspick_cond_destroy(cspick_cond_t *cond);
void cspick_thread_start(void);
void cspick_thread_exit(void);
void cspick_application_init(void);
void cspick_application_exit(void);
void cspick_init_context(cspick_mutex_t *impl, cspick_node_t *context, int number);

typedef cspick_mutex_t lock_mutex_t;
typedef cspick_node_t lock_context_t;
typedef cspick_cond_t lock_cond_t;

#define lock_mutex_create cspick_mutex_create
#define lock_mutex_lock cspick_mutex_lock
#define lock_mutex_trylock cspick_mutex_trylock
#define lock_mutex_unlock cspick_mutex_unlock
#define lock_mutex_destroy cspick_mutex_destroy
#define lock_cond_init cspick_cond_init
#define lock_cond_timedwait cspick_cond_timedwait
#define lock_cond_wait cspick_cond_wait
#define lock_cond_signal cspick_cond_signal
#define lock_cond_broadcast cspick_cond_broadcast
#define lock_cond_destroy cspick_cond_destroy
#define lock_thread_start cspick_thread_start
#define lock_thread_exit cspick_thread_exit
#define lock_application_init cspick_application_init
#define lock_application_exit cspick_application_exit
#define lock_init_context cspick_init_context

#endif // __cspick_H__
