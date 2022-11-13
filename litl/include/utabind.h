#ifndef __utabind_H__
#define __utabind_H__

#include "padding.h"
#define LOCK_ALGORITHM "UTABIND"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 0

typedef struct utabind_node {
    struct utabind_node *volatile next;
    char __pad0[pad_to_cache_line(sizeof(struct utabind_node *))];

    struct utabind_node *volatile secTail;
    char __pad1[pad_to_cache_line(sizeof(struct utabind_node *))];

    int status;
    int actcnt; /* Cnt of continuesly being an active waiter */
    char __pad2[pad_to_cache_line(sizeof(int) * 2)];

    volatile uint64_t spin;
    char __pad3[pad_to_cache_line(sizeof(uint64_t))];
} utabind_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct utabind_mutex {
    struct utabind_node *volatile tail;
    char __pad[pad_to_cache_line(sizeof(struct utabind_node*))];
} utabind_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef utabind_mutex_t lock_mutex_t;
typedef utabind_node_t lock_context_t;

typedef pthread_cond_t utabind_cond_t;
utabind_mutex_t *utabind_mutex_create(const pthread_mutexattr_t * attr);

int utabind_mutex_lock_cri(utabind_mutex_t * impl, utabind_node_t * me, int loc);
int utabind_mutex_lock(utabind_mutex_t * impl, utabind_node_t * me);
int utabind_mutex_trylock(utabind_mutex_t * impl, utabind_node_t * me);
void utabind_mutex_unlock(utabind_mutex_t * impl, utabind_node_t * me);
int utabind_mutex_destroy(utabind_mutex_t * lock);
int utabind_cond_init(utabind_cond_t * cond, const pthread_condattr_t * attr);
int utabind_cond_timedwait(utabind_cond_t * cond, utabind_mutex_t * lock,
            utabind_node_t * me, const struct timespec *ts);
int utabind_cond_wait(utabind_cond_t * cond, utabind_mutex_t * lock, utabind_node_t * me);
int utabind_cond_signal(utabind_cond_t * cond);
int utabind_cond_broadcast(utabind_cond_t * cond);
int utabind_cond_destroy(utabind_cond_t * cond);
void utabind_thread_start(void);
void utabind_thread_exit(void);
void utabind_application_init(void);
void utabind_application_exit(void);
void utabind_init_context(lock_mutex_t * impl, lock_context_t * context, int number);

void set_ux(int is_ux); /* utablocking interface */

#define lock_mutex_create utabind_mutex_create
#define lock_mutex_lock utabind_mutex_lock
#define lock_mutex_trylock utabind_mutex_trylock
#define lock_mutex_unlock utabind_mutex_unlock
#define lock_mutex_destroy utabind_mutex_destroy
#define lock_cond_init utabind_cond_init
#define lock_cond_timedwait utabind_cond_timedwait
#define lock_cond_wait utabind_cond_wait
#define lock_cond_signal utabind_cond_signal
#define lock_cond_broadcast utabind_cond_broadcast
#define lock_cond_destroy utabind_cond_destroy
#define lock_thread_start utabind_thread_start
#define lock_thread_exit utabind_thread_exit
#define lock_application_init utabind_application_init
#define lock_application_exit utabind_application_exit
#define lock_mutex_lock_cri utabind_mutex_lock_cri
#define lock_init_context utabind_init_context
#endif              // __utablocking_H__

