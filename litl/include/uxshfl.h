#ifndef __uxshfl_H__
#define __uxshfl_H__

#include <string.h>

#include "padding.h"
#define LOCK_ALGORITHM "UXSHFL"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1

/*
 * Bit manipulation (not used currently)
 * Will use just one variable of 4 byts to enclose the following:
 * 0-7:   locked or unlocked
 * 8-15:  shuffle leader or not
 * 16-31: shuffle count
 */
#define	_uxshfl_SET_MASK(type)	(((1U << _uxshfl_ ## type ## _BITS) - 1)\
                                 << _uxshfl_ ## type ## _OFFSET)

/* This is directly used by the tail bytes (2 bytes) */
#define _uxshfl_TAIL_IDX_OFFSET	(0)
#define _uxshfl_TAIL_IDX_BITS	2
#define _uxshfl_TAIL_IDX_MASK	_uxshfl_SET_MASK(TAIL_IDX)

#define _uxshfl_TAIL_CPU_OFFSET	(_uxshfl_TAIL_IDX_OFFSET + _uxshfl_TAIL_IDX_BITS)
#define _uxshfl_TAIL_CPU_BITS	(16 - _uxshfl_TAIL_CPU_OFFSET)
#define _uxshfl_TAIL_CPU_MASK	_uxshfl_SET_MASK(TAIL_CPU)

#define _uxshfl_TAIL_OFFSET	_uxshfl_TAIL_IDX_OFFSET
#define _uxshfl_TAIL_MASK		(_uxshfl_TAIL_IDX_MASK | _uxshfl_TAIL_CPU_MASK)

/* Use 1 bit for the NOSTEAL part */
#define _uxshfl_NOSTEAL_OFFSET     0
#define _uxshfl_NOSTEAL_BITS       1
#define _uxshfl_NOSTEAL_MASK       _uxshfl_SET_MASK(NOSTEAL)

/* We can support up to 127 sockets for NUMA-aware fastpath stealing */
#define _uxshfl_NUMA_ID_OFFSET     (_uxshfl_NOSTEAL_OFFSET + _uxshfl_NOSTEAL_BITS)
#define _uxshfl_NUMA_ID_BITS       7
#define _uxshfl_NUMA_ID_MASK       _uxshfl_SET_MASK(NUMA_ID)
#define _uxshfl_NUMA_ID_VAL(v)     ((v) & _uxshfl_NUMA_ID_MASK) >> _uxshfl_NUMA_ID_OFFSET

#define _uxshfl_LOCKED_OFFSET              0
#define _uxshfl_LOCKED_BITS                8
#define _uxshfl_LOCKED_NOSTEAL_OFFSET      (_uxshfl_LOCKED_OFFSET + _uxshfl_LOCKED_BITS)

#define uxshfl_NOSTEAL_VAL         1
#define uxshfl_STATUS_WAIT         0
#define uxshfl_STATUS_LOCKED       1
#define uxshfl_MAX_LOCK_COUNT      50
#define uxshfl_SERVE_COUNT         (255) /* max of 8 bits */

/* Arch utility */
static inline void smp_rmb(void)
{
    __asm __volatile("lfence":::"memory");
}

static inline void smp_cmb(void)
{
    __asm __volatile("":::"memory");
}

#define barrier()           smp_cmb()

static inline void __write_once_size(volatile void *p, void *res, int size)
{
        switch(size) {
        case 1: *(volatile uint8_t *)p = *(uint8_t *)res; break;
        case 2: *(volatile uint16_t *)p = *(uint16_t *)res; break;
        case 4: *(volatile uint32_t *)p = *(uint32_t *)res; break;
        case 8: *(volatile uint64_t *)p = *(uint64_t *)res; break;
        default:
                barrier();
                memcpy((void *)p, (const void *)res, size);
                barrier();
        }
}

static inline void __read_once_size(volatile void *p, void *res, int size)
{
        switch(size) {
        case 1: *(uint8_t *)res = *(volatile uint8_t *)p; break;
        case 2: *(uint16_t *)res = *(volatile uint16_t *)p; break;
        case 4: *(uint32_t *)res = *(volatile uint32_t *)p; break;
        case 8: *(uint64_t *)res = *(volatile uint64_t *)p; break;
        default:
                barrier();
                memcpy((void *)res, (const void *)p, size);
                barrier();
        }
}

#define WRITE_ONCE(x, val)                                      \
        ({                                                      \
         union { typeof(x) __val; char __c[1]; } __u =          \
                { .__val = (typeof(x)) (val) };                 \
        __write_once_size(&(x), __u.__c, sizeof(x));            \
        __u.__val;                                              \
         })

#define READ_ONCE(x)                                            \
        ({                                                      \
         union { typeof(x) __val; char __c[1]; } __u;           \
         __read_once_size(&(x), __u.__c, sizeof(x));            \
         __u.__val;                                             \
         })

#define smp_cas(__ptr, __old_val, __new_val)	\
        __sync_val_compare_and_swap(__ptr, __old_val, __new_val)
#define smp_swap(__ptr, __val)			\
	__sync_lock_test_and_set(__ptr, __val)
#define smp_faa(__ptr, __val)			\
	__sync_fetch_and_add(__ptr, __val)

typedef struct uxshfl_node {
    struct uxshfl_node *next;
    union {
        uint32_t locked;
        struct {
            uint8_t lstatus;
            uint8_t sleader;
            uint16_t wcount;
        };
    };
    int ux;
    int nid;
    int cid;
    struct uxshfl_node *last_visited;

    int lock_status;
    int type;
    char __pad2[pad_to_cache_line(sizeof(uint32_t))];
} uxshfl_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct uxshfl_mutex {
    struct uxshfl_node *tail;
    union {
        uint32_t val;
        struct {
            uint8_t locked;
            uint8_t no_stealing;
        };
        struct {
            uint16_t locked_no_stealing;
            uint8_t __pad[2];
        };
   };
    char __pad2[pad_to_cache_line(sizeof(uint32_t))];
#if COND_VAR
    pthread_mutex_t posix_lock;
    char __pad3[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
} uxshfl_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t uxshfl_cond_t;
uxshfl_mutex_t *uxshfl_mutex_create(const pthread_mutexattr_t *attr);
int uxshfl_mutex_lock(uxshfl_mutex_t *impl, uxshfl_node_t *me);
int uxshfl_mutex_trylock(uxshfl_mutex_t *impl, uxshfl_node_t *me);
void uxshfl_mutex_unlock(uxshfl_mutex_t *impl, uxshfl_node_t *me);
int uxshfl_mutex_destroy(uxshfl_mutex_t *lock);
int uxshfl_cond_init(uxshfl_cond_t *cond, const pthread_condattr_t *attr);
int uxshfl_cond_timedwait(uxshfl_cond_t *cond, uxshfl_mutex_t *lock, uxshfl_node_t *me,
                       const struct timespec *ts);
int uxshfl_cond_wait(uxshfl_cond_t *cond, uxshfl_mutex_t *lock, uxshfl_node_t *me);
int uxshfl_cond_signal(uxshfl_cond_t *cond);
int uxshfl_cond_broadcast(uxshfl_cond_t *cond);
int uxshfl_cond_destroy(uxshfl_cond_t *cond);
void uxshfl_thread_start(void);
void uxshfl_thread_exit(void);
void uxshfl_application_init(void);
void uxshfl_application_exit(void);
void uxshfl_init_context(uxshfl_mutex_t *impl, uxshfl_node_t *context, int number);

typedef uxshfl_mutex_t lock_mutex_t;
typedef uxshfl_node_t lock_context_t;
typedef uxshfl_cond_t lock_cond_t;

#define lock_mutex_create uxshfl_mutex_create
#define lock_mutex_lock uxshfl_mutex_lock
#define lock_mutex_trylock uxshfl_mutex_trylock
#define lock_mutex_unlock uxshfl_mutex_unlock
#define lock_mutex_destroy uxshfl_mutex_destroy
#define lock_cond_init uxshfl_cond_init
#define lock_cond_timedwait uxshfl_cond_timedwait
#define lock_cond_wait uxshfl_cond_wait
#define lock_cond_signal uxshfl_cond_signal
#define lock_cond_broadcast uxshfl_cond_broadcast
#define lock_cond_destroy uxshfl_cond_destroy
#define lock_thread_start uxshfl_thread_start
#define lock_thread_exit uxshfl_thread_exit
#define lock_application_init uxshfl_application_init
#define lock_application_exit uxshfl_application_exit
#define lock_init_context uxshfl_init_context

#endif // __uxshfl_H__