#ifndef __uta_H__
#define __uta_H__

#include "padding.h"
#define LOCK_ALGORITHM "UTA"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0
#define MAX_THREADS 16

#define UX_PRIORITY
#define CS_PRIORITY
#define CS_PREDICT

void set_starttime(int loc);
void set_endtime();
void set_cs(int cri_len);
void set_ux(int is_ux);
typedef struct uta_node {
	struct uta_node *volatile next;
    struct uta_node *volatile secTail;
	int cri_len;
	volatile uintptr_t spin __attribute__((aligned(L_CACHE_LINE_SIZE)));
	char __pad[pad_to_cache_line(sizeof(int) + sizeof(void *))];
} uta_node_t;

typedef struct uta_mutex {
#if COND_VAR
	pthread_mutex_t posix_lock;
	char __pad0[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
	struct uta_node *volatile tail;
	char __pad[pad_to_cache_line(sizeof(struct uta_node*))];
	 
	volatile int32_t adjust;
	char __pad1[pad_to_cache_line(sizeof(int32_t))];
	volatile int32_t threshold;
	char __pad2[pad_to_cache_line(sizeof(int32_t))];
	uint32_t batch;

} uta_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef uta_mutex_t lock_mutex_t;
typedef uta_node_t lock_context_t;

typedef struct {
    lock_mutex_t *lock_lock;
    char __pad0[pad_to_cache_line(sizeof(lock_mutex_t *))];

    uta_node_t lock_node[MAX_THREADS];
    char __pad1[pad_to_cache_line(sizeof(lock_context_t)*MAX_THREADS)];
} lock_transparent_mutex_t;

typedef pthread_cond_t uta_cond_t;
uta_mutex_t *uta_mutex_create(const pthread_mutexattr_t * attr);
lock_transparent_mutex_t *lock_create(const pthread_mutexattr_t *attr);
void uta_mutex_unlock_cri(uta_mutex_t * impl, uta_node_t * me);
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

#endif				// __uta_H__
