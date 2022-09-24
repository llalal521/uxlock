#ifndef __aslblock_H__
#define __aslblock_H__

#include "padding.h"
#define LOCK_ALGORITHM "ASLBLOCK"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1

typedef struct aslblock_node {
	struct aslblock_node *volatile next;
	volatile int spin;
	char __pad[pad_to_cache_line(sizeof(int) + sizeof(void *))];
} aslblock_node_t; 

typedef struct aslblock_mutex {
#if COND_VAR
	pthread_mutex_t posix_lock;
	char __pad0[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
	struct aslblock_node *volatile tail;
} aslblock_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t aslblock_cond_t;
aslblock_mutex_t *aslblock_mutex_create(const pthread_mutexattr_t * attr);
int aslblock_mutex_lock(aslblock_mutex_t * impl, aslblock_node_t * me);
int aslblock_mutex_trylock(aslblock_mutex_t * impl, aslblock_node_t * me);
void aslblock_mutex_unlock(aslblock_mutex_t * impl, aslblock_node_t * me);
int aslblock_mutex_destroy(aslblock_mutex_t * lock);
int aslblock_cond_init(aslblock_cond_t * cond, const pthread_condattr_t * attr);
int aslblock_cond_timedwait(aslblock_cond_t * cond, aslblock_mutex_t * lock,
			aslblock_node_t * me, const struct timespec *ts);
int aslblock_cond_wait(aslblock_cond_t * cond, aslblock_mutex_t * lock, aslblock_node_t * me);
int aslblock_cond_signal(aslblock_cond_t * cond);
int aslblock_cond_broadcast(aslblock_cond_t * cond);
int aslblock_cond_destroy(aslblock_cond_t * cond);
void aslblock_thread_start(void);
void aslblock_thread_exit(void);
void aslblock_application_init(void);
void aslblock_application_exit(void);
void aslblock_init_context(aslblock_mutex_t * impl, aslblock_node_t * context, int number);

typedef aslblock_mutex_t lock_mutex_t;
typedef aslblock_node_t lock_context_t;
typedef aslblock_cond_t lock_cond_t;

#define lock_mutex_create aslblock_mutex_create
#define lock_mutex_lock aslblock_mutex_lock
#define lock_mutex_trylock aslblock_mutex_trylock
#define lock_mutex_unlock aslblock_mutex_unlock
#define lock_mutex_destroy aslblock_mutex_destroy
#define lock_cond_init aslblock_cond_init
#define lock_cond_timedwait aslblock_cond_timedwait
#define lock_cond_wait aslblock_cond_wait
#define lock_cond_signal aslblock_cond_signal
#define lock_cond_broadcast aslblock_cond_broadcast
#define lock_cond_destroy aslblock_cond_destroy
#define lock_thread_start aslblock_thread_start
#define lock_thread_exit aslblock_thread_exit
#define lock_application_init aslblock_application_init
#define lock_application_exit aslblock_application_exit
#define lock_init_context aslblock_init_context

#endif				// __aslblock_H__
