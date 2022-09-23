#ifndef __cst_H__
#define __cst_H__

#include "padding.h"
#define LOCK_ALGORITHM "CST"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1



typedef struct cst_node {
	struct cst_node *volatile next;
	char __pad0[pad_to_cache_line(sizeof(struct cst_node *))];

	volatile uint64_t spin;
	char __pad1[pad_to_cache_line(sizeof(uint64_t))];

	struct cst_node *volatile secTail;
	char __pad2[pad_to_cache_line(sizeof(struct cst_node *))];

	int status;
	char __pad3[pad_to_cache_line(sizeof(int))];

	 int tid;
	char __pad4[pad_to_cache_line(sizeof(int))];

	volatile int wait;
	char __pad6[pad_to_cache_line(sizeof(int))];
} cst_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct cst_mutex {
	struct cst_node *volatile tail;
	char __pad[pad_to_cache_line(sizeof(struct cst_node*))];
	 
	volatile int64_t threshold;
	char __pad1[pad_to_cache_line(sizeof(int64_t))];
} cst_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef cst_mutex_t lock_mutex_t;
typedef cst_node_t lock_context_t;

typedef pthread_cond_t cst_cond_t;
cst_mutex_t *cst_mutex_create(const pthread_mutexattr_t * attr);

int cst_mutex_lock_cri(cst_mutex_t * impl, cst_node_t * me, int loc);
int cst_mutex_lock(cst_mutex_t * impl, cst_node_t * me);
int cst_mutex_trylock(cst_mutex_t * impl, cst_node_t * me);
void cst_mutex_unlock(cst_mutex_t * impl, cst_node_t * me);
int cst_mutex_destroy(cst_mutex_t * lock);
int cst_cond_init(cst_cond_t * cond, const pthread_condattr_t * attr);
int cst_cond_timedwait(cst_cond_t * cond, cst_mutex_t * lock,
			cst_node_t * me, const struct timespec *ts);
int cst_cond_wait(cst_cond_t * cond, cst_mutex_t * lock, cst_node_t * me);
int cst_cond_signal(cst_cond_t * cond);
int cst_cond_broadcast(cst_cond_t * cond);
int cst_cond_destroy(cst_cond_t * cond);
void cst_thread_start(void);
void cst_thread_exit(void);
void cst_application_init(void);
void cst_application_exit(void);
void cst_init_context(lock_mutex_t * impl, lock_context_t * context, int number);

void set_ux(int is_ux); /* cst interface */

#define lock_mutex_create cst_mutex_create
#define lock_mutex_lock cst_mutex_lock
#define lock_mutex_trylock cst_mutex_trylock
#define lock_mutex_unlock cst_mutex_unlock
#define lock_mutex_destroy cst_mutex_destroy
#define lock_cond_init cst_cond_init
#define lock_cond_timedwait cst_cond_timedwait
#define lock_cond_wait cst_cond_wait
#define lock_cond_signal cst_cond_signal
#define lock_cond_broadcast cst_cond_broadcast
#define lock_cond_destroy cst_cond_destroy
#define lock_thread_start cst_thread_start
#define lock_thread_exit cst_thread_exit
#define lock_application_init cst_application_init
#define lock_application_exit cst_application_exit
#define lock_mutex_lock_cri cst_mutex_lock_cri
#define lock_init_context cst_init_context
#endif				// __cst_H__
