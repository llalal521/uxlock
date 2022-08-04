#ifndef __uxactivekc_H__
#define __uxactivekc_H__

#include "padding.h"
#define LOCK_ALGORITHM "UXACTIVEKC"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct uxactivekc_node {
	struct uxactivekc_node *volatile next;
	volatile int spin;
	char __pad[pad_to_cache_line(sizeof(int) + sizeof(void *))];
} uxactivekc_node_t;

typedef struct uxactivekc_mutex {
#if COND_VAR
	pthread_mutex_t posix_lock;
	char __pad0[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
	volatile uint32_t grant;
	volatile uint32_t request;
	struct uxactivekc_node *volatile tail;
} uxactivekc_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t uxactivekc_cond_t;
uxactivekc_mutex_t *uxactivekc_mutex_create(const pthread_mutexattr_t * attr);
int uxactivekc_mutex_lock(uxactivekc_mutex_t * impl, uxactivekc_node_t * me);
int uxactivekc_mutex_trylock(uxactivekc_mutex_t * impl, uxactivekc_node_t * me);
void uxactivekc_mutex_unlock(uxactivekc_mutex_t * impl, uxactivekc_node_t * me);
int uxactivekc_mutex_destroy(uxactivekc_mutex_t * lock);
int uxactivekc_cond_init(uxactivekc_cond_t * cond, const pthread_condattr_t * attr);
int uxactivekc_cond_timedwait(uxactivekc_cond_t * cond, uxactivekc_mutex_t * lock,
			uxactivekc_node_t * me, const struct timespec *ts);
int uxactivekc_cond_wait(uxactivekc_cond_t * cond, uxactivekc_mutex_t * lock, uxactivekc_node_t * me);
int uxactivekc_cond_signal(uxactivekc_cond_t * cond);
int uxactivekc_cond_broadcast(uxactivekc_cond_t * cond);
int uxactivekc_cond_destroy(uxactivekc_cond_t * cond);
void uxactivekc_thread_start(void);
void uxactivekc_thread_exit(void);
void uxactivekc_application_init(void);
void uxactivekc_application_exit(void);
void uxactivekc_init_context(uxactivekc_mutex_t * impl, uxactivekc_node_t * context, int number);

typedef uxactivekc_mutex_t lock_mutex_t;
typedef uxactivekc_node_t lock_context_t;
typedef uxactivekc_cond_t lock_cond_t;

#define lock_mutex_create uxactivekc_mutex_create
#define lock_mutex_lock uxactivekc_mutex_lock
#define lock_mutex_trylock uxactivekc_mutex_trylock
#define lock_mutex_unlock uxactivekc_mutex_unlock
#define lock_mutex_destroy uxactivekc_mutex_destroy
#define lock_cond_init uxactivekc_cond_init
#define lock_cond_timedwait uxactivekc_cond_timedwait
#define lock_cond_wait uxactivekc_cond_wait
#define lock_cond_signal uxactivekc_cond_signal
#define lock_cond_broadcast uxactivekc_cond_broadcast
#define lock_cond_destroy uxactivekc_cond_destroy
#define lock_thread_start uxactivekc_thread_start
#define lock_thread_exit uxactivekc_thread_exit
#define lock_application_init uxactivekc_application_init
#define lock_application_exit uxactivekc_application_exit
#define lock_init_context uxactivekc_init_context

#endif				// __uxactivekc_H__
