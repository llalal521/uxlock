#ifndef __utascl_H__
#define __utascl_H__

#include "padding.h"
#define LOCK_ALGORITHM "utascl"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct utascl_node {
	struct utascl_node *volatile next;
	volatile int spin;
	long long hold_time;
	long long start_ts;
	char __pad[pad_to_cache_line(sizeof(int) + sizeof(void *) + sizeof(long long))];
} utascl_node_t; 

typedef struct utascl_mutex {
#if COND_VAR
	pthread_mutex_t posix_lock;
	char __pad0[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
	struct utascl_node *volatile tail;
	volatile long long max_hold_time;
	char __pad[pad_to_cache_line(sizeof(struct utascl_node *) + sizeof(long long))];
} utascl_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t utascl_cond_t;
utascl_mutex_t *utascl_mutex_create(const pthread_mutexattr_t * attr);
int utascl_mutex_lock(utascl_mutex_t * impl, utascl_node_t * me);
int utascl_mutex_trylock(utascl_mutex_t * impl, utascl_node_t * me);
void utascl_mutex_unlock(utascl_mutex_t * impl, utascl_node_t * me);
int utascl_mutex_destroy(utascl_mutex_t * lock);
int utascl_cond_init(utascl_cond_t * cond, const pthread_condattr_t * attr);
int utascl_cond_timedwait(utascl_cond_t * cond, utascl_mutex_t * lock,
			utascl_node_t * me, const struct timespec *ts);
int utascl_cond_wait(utascl_cond_t * cond, utascl_mutex_t * lock, utascl_node_t * me);
int utascl_cond_signal(utascl_cond_t * cond);
int utascl_cond_broadcast(utascl_cond_t * cond);
int utascl_cond_destroy(utascl_cond_t * cond);
void utascl_thread_start(void);
void utascl_thread_exit(void);
void utascl_application_init(void);
void utascl_application_exit(void);
void utascl_init_context(utascl_mutex_t * impl, utascl_node_t * context, int number);

typedef utascl_mutex_t lock_mutex_t;
typedef utascl_node_t lock_context_t;
typedef utascl_cond_t lock_cond_t;

#define lock_mutex_create utascl_mutex_create
#define lock_mutex_lock utascl_mutex_lock
#define lock_mutex_trylock utascl_mutex_trylock
#define lock_mutex_unlock utascl_mutex_unlock
#define lock_mutex_destroy utascl_mutex_destroy
#define lock_cond_init utascl_cond_init
#define lock_cond_timedwait utascl_cond_timedwait
#define lock_cond_wait utascl_cond_wait
#define lock_cond_signal utascl_cond_signal
#define lock_cond_broadcast utascl_cond_broadcast
#define lock_cond_destroy utascl_cond_destroy
#define lock_thread_start utascl_thread_start
#define lock_thread_exit utascl_thread_exit
#define lock_application_init utascl_application_init
#define lock_application_exit utascl_application_exit
#define lock_init_context utascl_init_context

#endif				// __utascl_H__
