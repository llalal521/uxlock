#ifndef __csupperbound_H__
#define __csupperbound_H__

#include "padding.h"
#define LOCK_ALGORITHM "CSUPPERBOUND"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct csupperbound_node {
	struct csupperbound_node *volatile next;
	volatile int spin;
	char __pad[pad_to_cache_line(sizeof(int) + sizeof(void *))];
} csupperbound_node_t; 

typedef struct csupperbound_mutex {
#if COND_VAR
	pthread_mutex_t posix_lock;
	char __pad0[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
	struct csupperbound_node *volatile tail;
} csupperbound_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t csupperbound_cond_t;
csupperbound_mutex_t *csupperbound_mutex_create(const pthread_mutexattr_t * attr);
int csupperbound_mutex_lock(csupperbound_mutex_t * impl, csupperbound_node_t * me);
int csupperbound_mutex_trylock(csupperbound_mutex_t * impl, csupperbound_node_t * me);
void csupperbound_mutex_unlock(csupperbound_mutex_t * impl, csupperbound_node_t * me);
int csupperbound_mutex_destroy(csupperbound_mutex_t * lock);
int csupperbound_cond_init(csupperbound_cond_t * cond, const pthread_condattr_t * attr);
int csupperbound_cond_timedwait(csupperbound_cond_t * cond, csupperbound_mutex_t * lock,
			csupperbound_node_t * me, const struct timespec *ts);
int csupperbound_cond_wait(csupperbound_cond_t * cond, csupperbound_mutex_t * lock, csupperbound_node_t * me);
int csupperbound_cond_signal(csupperbound_cond_t * cond);
int csupperbound_cond_broadcast(csupperbound_cond_t * cond);
int csupperbound_cond_destroy(csupperbound_cond_t * cond);
void csupperbound_thread_start(void);
void csupperbound_thread_exit(void);
void csupperbound_application_init(void);
void csupperbound_application_exit(void);
void csupperbound_init_context(csupperbound_mutex_t * impl, csupperbound_node_t * context, int number);

typedef csupperbound_mutex_t lock_mutex_t;
typedef csupperbound_node_t lock_context_t;
typedef csupperbound_cond_t lock_cond_t;

#define lock_mutex_create csupperbound_mutex_create
#define lock_mutex_lock csupperbound_mutex_lock
#define lock_mutex_trylock csupperbound_mutex_trylock
#define lock_mutex_unlock csupperbound_mutex_unlock
#define lock_mutex_destroy csupperbound_mutex_destroy
#define lock_cond_init csupperbound_cond_init
#define lock_cond_timedwait csupperbound_cond_timedwait
#define lock_cond_wait csupperbound_cond_wait
#define lock_cond_signal csupperbound_cond_signal
#define lock_cond_broadcast csupperbound_cond_broadcast
#define lock_cond_destroy csupperbound_cond_destroy
#define lock_thread_start csupperbound_thread_start
#define lock_thread_exit csupperbound_thread_exit
#define lock_application_init csupperbound_application_init
#define lock_application_exit csupperbound_application_exit
#define lock_init_context csupperbound_init_context

#endif				// __csupperbound_H__
