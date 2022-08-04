#ifndef __uxactivekcf_H__
#define __uxactivekcf_H__

#include "padding.h"
#define LOCK_ALGORITHM "UXACTIVEKCF"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct uxactivekcf_node {
	struct uxactivekcf_node *volatile next;
	volatile int spin;
	char __pad[pad_to_cache_line(sizeof(int) + sizeof(void *))];
} uxactivekcf_node_t;

typedef struct uxactivekcf_mutex {
#if COND_VAR
	pthread_mutex_t posix_lock;
	char __pad0[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
	struct uxactivekcf_node *volatile tail;
} uxactivekcf_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t uxactivekcf_cond_t;
uxactivekcf_mutex_t *uxactivekcf_mutex_create(const pthread_mutexattr_t * attr);
int uxactivekcf_mutex_lock(uxactivekcf_mutex_t * impl, uxactivekcf_node_t * me);
int uxactivekcf_mutex_trylock(uxactivekcf_mutex_t * impl, uxactivekcf_node_t * me);
void uxactivekcf_mutex_unlock(uxactivekcf_mutex_t * impl, uxactivekcf_node_t * me);
int uxactivekcf_mutex_destroy(uxactivekcf_mutex_t * lock);
int uxactivekcf_cond_init(uxactivekcf_cond_t * cond, const pthread_condattr_t * attr);
int uxactivekcf_cond_timedwait(uxactivekcf_cond_t * cond, uxactivekcf_mutex_t * lock,
			uxactivekcf_node_t * me, const struct timespec *ts);
int uxactivekcf_cond_wait(uxactivekcf_cond_t * cond, uxactivekcf_mutex_t * lock, uxactivekcf_node_t * me);
int uxactivekcf_cond_signal(uxactivekcf_cond_t * cond);
int uxactivekcf_cond_broadcast(uxactivekcf_cond_t * cond);
int uxactivekcf_cond_destroy(uxactivekcf_cond_t * cond);
void uxactivekcf_thread_start(void);
void uxactivekcf_thread_exit(void);
void uxactivekcf_application_init(void);
void uxactivekcf_application_exit(void);
void uxactivekcf_init_context(uxactivekcf_mutex_t * impl, uxactivekcf_node_t * context, int number);

typedef uxactivekcf_mutex_t lock_mutex_t;
typedef uxactivekcf_node_t lock_context_t;
typedef uxactivekcf_cond_t lock_cond_t;

#define lock_mutex_create uxactivekcf_mutex_create
#define lock_mutex_lock uxactivekcf_mutex_lock
#define lock_mutex_trylock uxactivekcf_mutex_trylock
#define lock_mutex_unlock uxactivekcf_mutex_unlock
#define lock_mutex_destroy uxactivekcf_mutex_destroy
#define lock_cond_init uxactivekcf_cond_init
#define lock_cond_timedwait uxactivekcf_cond_timedwait
#define lock_cond_wait uxactivekcf_cond_wait
#define lock_cond_signal uxactivekcf_cond_signal
#define lock_cond_broadcast uxactivekcf_cond_broadcast
#define lock_cond_destroy uxactivekcf_cond_destroy
#define lock_thread_start uxactivekcf_thread_start
#define lock_thread_exit uxactivekcf_thread_exit
#define lock_application_init uxactivekcf_application_init
#define lock_application_exit uxactivekcf_application_exit
#define lock_init_context uxactivekcf_init_context

#endif				// __uxactivekcf_H__
