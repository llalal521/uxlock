#ifndef __uxactive_H__
#define __uxactive_H__

#include "padding.h"
#define LOCK_ALGORITHM "UXACTIVE"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct uxactive_node {
	struct uxactive_node *volatile next;
	volatile int spin;
	char __pad[pad_to_cache_line(sizeof(int) + sizeof(void *))];
} uxactive_node_t; 

typedef struct uxactive_mutex {
#if COND_VAR
	pthread_mutex_t posix_lock;
	char __pad0[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
	struct uxactive_node *volatile tail;
} uxactive_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t uxactive_cond_t;
uxactive_mutex_t *uxactive_mutex_create(const pthread_mutexattr_t * attr);
int uxactive_mutex_lock(uxactive_mutex_t * impl, uxactive_node_t * me);
int uxactive_mutex_trylock(uxactive_mutex_t * impl, uxactive_node_t * me);
void uxactive_mutex_unlock(uxactive_mutex_t * impl, uxactive_node_t * me);
int uxactive_mutex_destroy(uxactive_mutex_t * lock);
int uxactive_cond_init(uxactive_cond_t * cond, const pthread_condattr_t * attr);
int uxactive_cond_timedwait(uxactive_cond_t * cond, uxactive_mutex_t * lock,
			uxactive_node_t * me, const struct timespec *ts);
int uxactive_cond_wait(uxactive_cond_t * cond, uxactive_mutex_t * lock, uxactive_node_t * me);
int uxactive_cond_signal(uxactive_cond_t * cond);
int uxactive_cond_broadcast(uxactive_cond_t * cond);
int uxactive_cond_destroy(uxactive_cond_t * cond);
void uxactive_thread_start(void);
void uxactive_thread_exit(void);
void uxactive_application_init(void);
void uxactive_application_exit(void);
void uxactive_init_context(uxactive_mutex_t * impl, uxactive_node_t * context, int number);

typedef uxactive_mutex_t lock_mutex_t;
typedef uxactive_node_t lock_context_t;
typedef uxactive_cond_t lock_cond_t;

#define lock_mutex_create uxactive_mutex_create
#define lock_mutex_lock uxactive_mutex_lock
#define lock_mutex_trylock uxactive_mutex_trylock
#define lock_mutex_unlock uxactive_mutex_unlock
#define lock_mutex_destroy uxactive_mutex_destroy
#define lock_cond_init uxactive_cond_init
#define lock_cond_timedwait uxactive_cond_timedwait
#define lock_cond_wait uxactive_cond_wait
#define lock_cond_signal uxactive_cond_signal
#define lock_cond_broadcast uxactive_cond_broadcast
#define lock_cond_destroy uxactive_cond_destroy
#define lock_thread_start uxactive_thread_start
#define lock_thread_exit uxactive_thread_exit
#define lock_application_init uxactive_application_init
#define lock_application_exit uxactive_application_exit
#define lock_init_context uxactive_init_context

#endif				// __uxactive_H__
