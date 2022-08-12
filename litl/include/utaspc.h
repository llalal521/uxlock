#ifndef __utaspc_H__
#define __utaspc_H__

#include "padding.h"
#define LOCK_ALGORITHM "utaspc"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct utaspc_node {
	struct utaspc_node *volatile next;
	char __pad0[pad_to_cache_line(sizeof(struct utaspc_node *))];

	volatile uint64_t spin;
	char __pad1[pad_to_cache_line(sizeof(uint64_t))];

	struct utaspc_node *volatile secTail;
	char __pad2[pad_to_cache_line(sizeof(struct utaspc_node *))];

	int cri_len;
	char __pad3[pad_to_cache_line(sizeof(int))];
} utaspc_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct utaspc_mutex {
	struct utaspc_node *volatile tail;
	char __pad[pad_to_cache_line(sizeof(struct utaspc_node*))];
	 
	int64_t threshold;
	char __pad1[pad_to_cache_line(sizeof(int64_t))];
} utaspc_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef utaspc_mutex_t lock_mutex_t;
typedef utaspc_node_t lock_context_t;

typedef pthread_cond_t utaspc_cond_t;
utaspc_mutex_t *utaspc_mutex_create(const pthread_mutexattr_t * attr);

int utaspc_mutex_lock_cri(utaspc_mutex_t * impl, utaspc_node_t * me, int loc);
int utaspc_mutex_lock(utaspc_mutex_t * impl, utaspc_node_t * me);
int utaspc_mutex_trylock(utaspc_mutex_t * impl, utaspc_node_t * me);
void utaspc_mutex_unlock(utaspc_mutex_t * impl, utaspc_node_t * me);
int utaspc_mutex_destroy(utaspc_mutex_t * lock);
int utaspc_cond_init(utaspc_cond_t * cond, const pthread_condattr_t * attr);
int utaspc_cond_timedwait(utaspc_cond_t * cond, utaspc_mutex_t * lock,
			utaspc_node_t * me, const struct timespec *ts);
int utaspc_cond_wait(utaspc_cond_t * cond, utaspc_mutex_t * lock, utaspc_node_t * me);
int utaspc_cond_signal(utaspc_cond_t * cond);
int utaspc_cond_broadcast(utaspc_cond_t * cond);
int utaspc_cond_destroy(utaspc_cond_t * cond);
void utaspc_thread_start(void);
void utaspc_thread_exit(void);
void utaspc_application_init(void);
void utaspc_application_exit(void);
void utaspc_init_context(lock_mutex_t * impl, lock_context_t * context, int number);

void set_ux(int is_ux); /* utaspc interface */

#define lock_mutex_create utaspc_mutex_create
#define lock_mutex_lock utaspc_mutex_lock
#define lock_mutex_trylock utaspc_mutex_trylock
#define lock_mutex_unlock utaspc_mutex_unlock
#define lock_mutex_destroy utaspc_mutex_destroy
#define lock_cond_init utaspc_cond_init
#define lock_cond_timedwait utaspc_cond_timedwait
#define lock_cond_wait utaspc_cond_wait
#define lock_cond_signal utaspc_cond_signal
#define lock_cond_broadcast utaspc_cond_broadcast
#define lock_cond_destroy utaspc_cond_destroy
#define lock_thread_start utaspc_thread_start
#define lock_thread_exit utaspc_thread_exit
#define lock_application_init utaspc_application_init
#define lock_application_exit utaspc_application_exit
#define lock_mutex_lock_cri utaspc_mutex_lock_cri
#define lock_init_context utaspc_init_context
#endif				// __utaspc_H__
