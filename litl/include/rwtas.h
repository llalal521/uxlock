#ifndef __rwtas_H__
#define __rwtas_H__

#include <stdint.h>
#include "padding.h"
#define LOCK_ALGORITHM "RWTAS"
#define NEED_CONTEXT 0
#define SUPPORT_WAITING 0

typedef struct rwtas_mutex {
    volatile uint8_t spin_lock __attribute__((aligned(L_CACHE_LINE_SIZE)));
    char __pad[pad_to_cache_line(sizeof(uint8_t))];
#if COND_VAR
    pthread_mutex_t posix_lock;
#endif
} rwtas_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t rwtas_cond_t;
typedef void *rwtas_context_t; // Unused, take the less space as possible

// rwlock type
#define MAX_RW UINT8_MAX
#define W_MASK 0x100
typedef uint8_t rw_data_t;
typedef uint16_t all_data_t;

typedef struct rwtas_rwlock_data {
    volatile rw_data_t read_lock;
    volatile rw_data_t write_lock;
} rwtas_rwlock_data;

typedef struct rwtas_rwlock {
    union {
        rwtas_rwlock_data rw;
        volatile all_data_t lock_data;
    };
    char __pad[pad_to_cache_line(sizeof(all_data_t))];
#if COND_VAR
    pthread_rwlock_t posix_lock;
#endif
} rwtas_rwlock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));


rwtas_mutex_t *rwtas_mutex_create(const pthread_mutexattr_t *attr);
int rwtas_mutex_lock(rwtas_mutex_t *impl, rwtas_context_t *me);
int rwtas_mutex_trylock(rwtas_mutex_t *impl, rwtas_context_t *me);
void rwtas_mutex_unlock(rwtas_mutex_t *impl, rwtas_context_t *me);
int rwtas_mutex_destroy(rwtas_mutex_t *lock);
int rwtas_cond_init(rwtas_cond_t *cond, const pthread_condattr_t *attr);
int rwtas_cond_timedwait(rwtas_cond_t *cond, rwtas_mutex_t *lock,
                        rwtas_context_t *me, const struct timespec *ts);
int rwtas_cond_wait(rwtas_cond_t *cond, rwtas_mutex_t *lock, rwtas_context_t *me);
int rwtas_cond_signal(rwtas_cond_t *cond);
int rwtas_cond_broadcast(rwtas_cond_t *cond);
int rwtas_cond_destroy(rwtas_cond_t *cond);
void rwtas_thread_start(void);
void rwtas_thread_exit(void);
void rwtas_application_init(void);
void rwtas_application_exit(void);
void rwtas_init_context(rwtas_mutex_t *impl, rwtas_context_t *context, int number);

// rwlock method
rwtas_rwlock_t *rwtas_rwlock_create(const pthread_rwlockattr_t *attr);
int rwtas_rwlock_rdlock(rwtas_rwlock_t *impl, rwtas_context_t *me);
int rwtas_rwlock_wrlock(rwtas_rwlock_t *impl, rwtas_context_t *me);
int rwtas_rwlock_tryrdlock(rwtas_rwlock_t *impl, rwtas_context_t *me);
int rwtas_rwlock_trywrlock(rwtas_rwlock_t *impl, rwtas_context_t *me);
int rwtas_rwlock_unlock(rwtas_rwlock_t *impl, rwtas_context_t *me);
int rwtas_rwlock_destroy(rwtas_rwlock_t *lock);



typedef rwtas_mutex_t lock_mutex_t;
typedef rwtas_context_t lock_context_t;
typedef rwtas_cond_t lock_cond_t;
typedef rwtas_rwlock_t lock_rwlock_t;

// Define library function ptr
#define lock_mutex_create rwtas_mutex_create
#define lock_mutex_lock rwtas_mutex_lock
#define lock_mutex_trylock rwtas_mutex_trylock
#define lock_mutex_unlock rwtas_mutex_unlock
#define lock_mutex_destroy rwtas_mutex_destroy
#define lock_cond_init rwtas_cond_init
#define lock_cond_timedwait rwtas_cond_timedwait
#define lock_cond_wait rwtas_cond_wait
#define lock_cond_signal rwtas_cond_signal
#define lock_cond_broadcast rwtas_cond_broadcast
#define lock_cond_destroy rwtas_cond_destroy
#define lock_thread_start rwtas_thread_start
#define lock_thread_exit rwtas_thread_exit
#define lock_application_init rwtas_application_init
#define lock_application_exit rwtas_application_exit
#define lock_init_context rwtas_init_context

// rwlock method define
#define lock_rwlock_create rwtas_rwlock_create
#define lock_rwlock_rdlock rwtas_rwlock_rdlock
#define lock_rwlock_wrlock rwtas_rwlock_wrlock
#define lock_rwlock_tryrdlock rwtas_rwlock_tryrdlock
#define lock_rwlock_trywrlock rwtas_rwlock_trywrlock
#define lock_rwlock_unlock rwtas_rwlock_unlock
#define lock_rwlock_destroy rwtas_rwlock_destroy


#endif // __rwtas_H__
