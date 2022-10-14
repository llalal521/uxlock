#ifndef __utablocking_H__
#define __utablocking_H__

#include "padding.h"
#define LOCK_ALGORITHM "UTABLOCKING"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct utablocking_node {
	struct utablocking_node *volatile next;
	char __pad0[pad_to_cache_line(sizeof(struct utablocking_node *))];

	struct utablocking_node *volatile secTail;
	char __pad1[pad_to_cache_line(sizeof(struct utablocking_node *))];

	int status;
	int actcnt; /* Cnt of continuesly being an active waiter */
	char __pad2[pad_to_cache_line(sizeof(int) * 2)];

	volatile uint64_t spin;
	char __pad3[pad_to_cache_line(sizeof(uint64_t))];
} utablocking_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct utablocking_mutex {
	struct utablocking_node *volatile tail;
	char __pad[pad_to_cache_line(sizeof(struct utablocking_node*))];
} utablocking_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef utablocking_mutex_t lock_mutex_t;
typedef utablocking_node_t lock_context_t;

typedef pthread_cond_t utablocking_cond_t;
utablocking_mutex_t *utablocking_mutex_create(const pthread_mutexattr_t * attr);

int utablocking_mutex_lock_cri(utablocking_mutex_t * impl, utablocking_node_t * me, int loc);
int utablocking_mutex_lock(utablocking_mutex_t * impl, utablocking_node_t * me);
int utablocking_mutex_trylock(utablocking_mutex_t * impl, utablocking_node_t * me);
void utablocking_mutex_unlock(utablocking_mutex_t * impl, utablocking_node_t * me);
int utablocking_mutex_destroy(utablocking_mutex_t * lock);
int utablocking_cond_init(utablocking_cond_t * cond, const pthread_condattr_t * attr);
int utablocking_cond_timedwait(utablocking_cond_t * cond, utablocking_mutex_t * lock,
			utablocking_node_t * me, const struct timespec *ts);
int utablocking_cond_wait(utablocking_cond_t * cond, utablocking_mutex_t * lock, utablocking_node_t * me);
int utablocking_cond_signal(utablocking_cond_t * cond);
int utablocking_cond_broadcast(utablocking_cond_t * cond);
int utablocking_cond_destroy(utablocking_cond_t * cond);
void utablocking_thread_start(void);
void utablocking_thread_exit(void);
void utablocking_application_init(void);
void utablocking_application_exit(void);
void utablocking_init_context(lock_mutex_t * impl, lock_context_t * context, int number);

void set_ux(int is_ux); /* utablocking interface */

#define lock_mutex_create utablocking_mutex_create
#define lock_mutex_lock utablocking_mutex_lock
#define lock_mutex_trylock utablocking_mutex_trylock
#define lock_mutex_unlock utablocking_mutex_unlock
#define lock_mutex_destroy utablocking_mutex_destroy
#define lock_cond_init utablocking_cond_init
#define lock_cond_timedwait utablocking_cond_timedwait
#define lock_cond_wait utablocking_cond_wait
#define lock_cond_signal utablocking_cond_signal
#define lock_cond_broadcast utablocking_cond_broadcast
#define lock_cond_destroy utablocking_cond_destroy
#define lock_thread_start utablocking_thread_start
#define lock_thread_exit utablocking_thread_exit
#define lock_application_init utablocking_application_init
#define lock_application_exit utablocking_application_exit
#define lock_mutex_lock_cri utablocking_mutex_lock_cri
#define lock_init_context utablocking_init_context
#endif				// __utablocking_H__

