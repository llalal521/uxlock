#ifndef __utafts_H__
#define __utafts_H__

#include "padding.h"
#define LOCK_ALGORITHM "UTAFTS"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct utafts_node {
	struct utafts_node *volatile next;
	char __pad0[pad_to_cache_line(sizeof(struct utafts_node *))];

	volatile uint64_t spin;
	char __pad1[pad_to_cache_line(sizeof(uint64_t))];

	struct utafts_node *volatile secTail;
	char __pad2[pad_to_cache_line(sizeof(struct utafts_node *))];

	int64_t remain_window;
	char __pad3[pad_to_cache_line(sizeof(uint64_t))];

	uint64_t start_ts;
	int id; /* Debug use */
	char __pad4[pad_to_cache_line(sizeof(uint64_t) + sizeof(int))];
} utafts_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct utafts_mutex {
	struct utafts_node *volatile tail;
	char __pad0[pad_to_cache_line(sizeof(struct utafts_node*))];

#ifdef ADJUSTABLE_WINDOW
	int64_t refill_window;
	char __pad1[pad_to_cache_line(sizeof(uint64_t))];
#endif
} utafts_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef utafts_mutex_t lock_mutex_t;
typedef utafts_node_t lock_context_t;

typedef pthread_cond_t utafts_cond_t;
utafts_mutex_t *utafts_mutex_create(const pthread_mutexattr_t * attr);

int utafts_mutex_lock_cri(utafts_mutex_t * impl, utafts_node_t * me, int loc);
int utafts_mutex_lock(utafts_mutex_t * impl, utafts_node_t * me);
int utafts_mutex_trylock(utafts_mutex_t * impl, utafts_node_t * me);
void utafts_mutex_unlock(utafts_mutex_t * impl, utafts_node_t * me);
int utafts_mutex_destroy(utafts_mutex_t * lock);
int utafts_cond_init(utafts_cond_t * cond, const pthread_condattr_t * attr);
int utafts_cond_timedwait(utafts_cond_t * cond, utafts_mutex_t * lock,
			utafts_node_t * me, const struct timespec *ts);
int utafts_cond_wait(utafts_cond_t * cond, utafts_mutex_t * lock, utafts_node_t * me);
int utafts_cond_signal(utafts_cond_t * cond);
int utafts_cond_broadcast(utafts_cond_t * cond);
int utafts_cond_destroy(utafts_cond_t * cond);
void utafts_thread_start(void);
void utafts_thread_exit(void);
void utafts_application_init(void);
void utafts_application_exit(void);
void utafts_init_context(lock_mutex_t * impl, lock_context_t * context, int number);

void set_ux(int is_ux); /* utafts interface */

#define lock_mutex_create utafts_mutex_create
#define lock_mutex_lock utafts_mutex_lock
#define lock_mutex_trylock utafts_mutex_trylock
#define lock_mutex_unlock utafts_mutex_unlock
#define lock_mutex_destroy utafts_mutex_destroy
#define lock_cond_init utafts_cond_init
#define lock_cond_timedwait utafts_cond_timedwait
#define lock_cond_wait utafts_cond_wait
#define lock_cond_signal utafts_cond_signal
#define lock_cond_broadcast utafts_cond_broadcast
#define lock_cond_destroy utafts_cond_destroy
#define lock_thread_start utafts_thread_start
#define lock_thread_exit utafts_thread_exit
#define lock_application_init utafts_application_init
#define lock_application_exit utafts_application_exit
#define lock_mutex_lock_cri utafts_mutex_lock_cri
#define lock_init_context utafts_init_context
#endif				// __utafts_H__
