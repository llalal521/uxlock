#ifndef __uta_H__
#define __uta_H__

#include "padding.h"
#define LOCK_ALGORITHM "UTA"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct uta_node {
	struct uta_node *volatile next;
	char __pad0[pad_to_cache_line(sizeof(struct uta_node *))];

	volatile uint64_t spin;
	char __pad1[pad_to_cache_line(sizeof(uint64_t))];

	struct uta_node *volatile secTail;
	char __pad2[pad_to_cache_line(sizeof(struct uta_node *))];

	int cri_len;
	char __pad3[pad_to_cache_line(sizeof(int))];
} uta_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct uta_mutex {
	struct uta_node *volatile tail;
	char __pad[pad_to_cache_line(sizeof(struct uta_node*))];
	 
	int64_t threshold;
	char __pad1[pad_to_cache_line(sizeof(int64_t))];
} uta_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef uta_mutex_t lock_mutex_t;
typedef uta_node_t lock_context_t;

typedef pthread_cond_t uta_cond_t;
uta_mutex_t *uta_mutex_create(const pthread_mutexattr_t * attr);

int uta_mutex_lock_cri(uta_mutex_t * impl, uta_node_t * me, int loc);
int uta_mutex_lock(uta_mutex_t * impl, uta_node_t * me);
int uta_mutex_trylock(uta_mutex_t * impl, uta_node_t * me);
void uta_mutex_unlock(uta_mutex_t * impl, uta_node_t * me);
int uta_mutex_destroy(uta_mutex_t * lock);
int uta_cond_init(uta_cond_t * cond, const pthread_condattr_t * attr);
int uta_cond_timedwait(uta_cond_t * cond, uta_mutex_t * lock,
			uta_node_t * me, const struct timespec *ts);
int uta_cond_wait(uta_cond_t * cond, uta_mutex_t * lock, uta_node_t * me);
int uta_cond_signal(uta_cond_t * cond);
int uta_cond_broadcast(uta_cond_t * cond);
int uta_cond_destroy(uta_cond_t * cond);
void uta_thread_start(void);
void uta_thread_exit(void);
void uta_application_init(void);
void uta_application_exit(void);
void uta_init_context(lock_mutex_t * impl, lock_context_t * context, int number);

void set_ux(int is_ux); /* uta interface */

#define lock_mutex_create uta_mutex_create
#define lock_mutex_lock uta_mutex_lock
#define lock_mutex_trylock uta_mutex_trylock
#define lock_mutex_unlock uta_mutex_unlock
#define lock_mutex_destroy uta_mutex_destroy
#define lock_cond_init uta_cond_init
#define lock_cond_timedwait uta_cond_timedwait
#define lock_cond_wait uta_cond_wait
#define lock_cond_signal uta_cond_signal
#define lock_cond_broadcast uta_cond_broadcast
#define lock_cond_destroy uta_cond_destroy
#define lock_thread_start uta_thread_start
#define lock_thread_exit uta_thread_exit
#define lock_application_init uta_application_init
#define lock_application_exit uta_application_exit
#define lock_mutex_lock_cri uta_mutex_lock_cri
#define lock_init_context uta_init_context
#endif				// __uta_H__
