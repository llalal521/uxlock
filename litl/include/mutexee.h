/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Hugo Guiroux <hugo.guiroux at gmail dot com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of his software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __mutexee_H__
#define __mutexee_H__

#include "padding.h"
#define LOCK_ALGORITHM "MUTEXEE"
#define NEED_CONTEXT 0
#define SUPPORT_WAITING 0

  /* ******************************************************************************** */
  /* settings *********************************************************************** */
#define PADDING        1	/* padd locks/conditionals to cache-line */
#define FREQ_CPU_GHZ   2.8	/* core frequency in GHz */
#define REPLACE_MUTEX  1	/* ovewrite the pthread_[mutex|cond] functions */
#define MUTEXEE_SPIN_TRIES_LOCK       8192	/* spinning retries before futex */
#define MUTEXEE_SPIN_TRIES_LOCK_MIN   1024	/* spinning retries before futex */
#define MUTEXEE_SPIN_TRIES_UNLOCK     384	/* spinning retries before futex wake */
#define MUTEXEE_SPIN_TRIES_UNLOCK_MIN 128	/* spinning retries before futex wake */

#define MUTEXEE_DO_ADAP             0
#define MUTEXEE_ADAP_EVERY          2047
#define MUTEXEE_RETRY_SPIN_EVERY     8
#define MUTEXEE_RETRY_SPIN_EVERY_MAX 32
#define MUTEXEE_FUTEX_LIM           128
#define MUTEXEE_FUTEX_LIM_MAX       256
#define MUTEXEE_PRINT               0	/* print debug output  */

#define MUTEXEE_FAIR 1
#ifndef MUTEXEE_FAIR
#define MUTEXEE_FAIR              1	/* enable or not mechanisms for capping
					   the maximum tail latency of the lock */
#endif

#if MUTEXEE_FAIR > 0
#define LOCK_IN_NAME "MUTEXEE-FAIR"
#else
#define LOCK_IN_NAME "MUTEXEE"
#endif

#define MUTEXEE_FTIMEOUTS 0	/* timeout seconds */
#ifndef MUTEXEE_FTIMEOUT
#define MUTEXEE_FTIMEOUT   50000000	/* timeout nanoseconds - max 1e9-1
					   if you want to set it to more than 1 sec
					   use MUTEXEE_FTIMEOUTS */
#endif

#if MUTEXEE_DO_ADAP == 1
#define MUTEXEE_ADAP(d)	    d
#else
#define MUTEXEE_ADAP(d)
#endif
  /* ******************************************************************************** */

#if defined(PAUSE_IN)
#undef PAUSE_IN
#define PAUSE_IN()				\
  asm volatile ("mfence");
#endif

static inline void mutexee_cdelay(const int cycles)
{
	int cy = cycles;
	while (cy--) {
		asm volatile ("");
	}
}

  //Swap uint32_t
static inline uint32_t
mutexee_swap_uint32(volatile uint32_t * target, uint32_t x)
{
	asm volatile ("xchgl %0,%1":"=r" ((uint32_t) x)
		      :"m"(*(volatile uint32_t *)target), "0"((uint32_t) x)
		      :"memory");

	return x;
}

  //Swap uint8_t
static inline uint8_t mutexee_swap_uint8(volatile uint8_t * target, uint8_t x)
{
	asm volatile ("xchgb %0,%1":"=r" ((uint8_t) x)
		      :"m"(*(volatile uint8_t *)target), "0"((uint8_t) x)
		      :"memory");

	return x;
}

#define mutexee_cas(a, b, c) __sync_val_compare_and_swap(a, b, c)
#define xchg_32(a, b)    mutexee_swap_uint32((uint32_t*) a, b)
#define xchg_8(a, b)     mutexee_swap_uint8((uint8_t*) a, b)
#define atomic_add(a, b) __sync_fetch_and_add(a, b)

#define CACHE_LINE_SIZE 64

typedef __attribute__((aligned(CACHE_LINE_SIZE)))
struct mutexee_lock {
	union {
		volatile unsigned u;
		struct {
			volatile unsigned char locked;
			volatile unsigned char contended;
		} b;
	} l;
	uint8_t padding[4];
	/* uint8_t padding_cl[56]; */

	unsigned int n_spins;
	unsigned int n_spins_unlock;
	size_t n_acq;
	unsigned int n_miss;
	unsigned int n_miss_limit;
	unsigned int is_futex;
	unsigned int n_acq_first_sleep;
	unsigned int retry_spin_every;
	unsigned int padding3;
	uint8_t padding2[CACHE_LINE_SIZE - 6 * sizeof(size_t)];
} mutexee_mutex_t;

#define STATIC_ASSERT(a, msg)           _Static_assert ((a), msg);

  /* STATIC_ASSERT((sizeof(mutexee_mutex_t) == 64) || (sizeof(mutexee_mutex_t) == 4),  */
  /*            "sizeof(mutexee_mutex_t)"); */

#define MUTEXEE_INITIALIZER				\
  {							\
    .l.u = 0,						\
      .n_spins = MUTEXEE_SPIN_TRIES_LOCK,		\
      .n_spins_unlock = MUTEXEE_SPIN_TRIES_UNLOCK,	\
      .n_acq = 0,	              			\
      .n_miss = 0,					\
      .n_miss_limit = MUTEXEE_FUTEX_LIM,		\
      .is_futex = 0,					\
      .n_acq_first_sleep = 0,				\
      .retry_spin_every = MUTEXEE_RETRY_SPIN_EVERY,	\
      }

typedef struct upmutex_cond1 {
	mutexee_mutex_t *m;
	int seq;
	int pad;
#if PADDING == 1
	uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} upmutex_cond1_t;

typedef pthread_cond_t mutexee_cond_t;
#define UPMUTEX_COND1_INITIALIZER {NULL, 0, 0}
typedef mutexee_cond_t mutexee_cond_t;
typedef void *mutexee_context_t;	// Unused, take the less space
					   // as possible

mutexee_mutex_t *mutexee_mutex_create(const pthread_mutexattr_t * attr);
int mutexee_mutex_lock(mutexee_mutex_t * impl, mutexee_context_t * me);
static inline int mutexee_mutex_trylock(mutexee_mutex_t * impl,
					mutexee_context_t * me);
int mutexee_mutex_unlock(mutexee_mutex_t * impl, mutexee_context_t * me);
int mutexee_mutex_destroy(mutexee_mutex_t * lock);
int mutexee_cond_init(mutexee_cond_t * cond, const pthread_condattr_t * attr);
int mutexee_cond_timedwait(mutexee_cond_t * cond,
			   mutexee_mutex_t * lock,
			   mutexee_context_t * me, const struct timespec *ts);
int mutexee_cond_wait(mutexee_cond_t * cond,
		      mutexee_mutex_t * lock, mutexee_context_t * me);
int mutexee_cond_signal(mutexee_cond_t * cond);
int mutexee_cond_broadcast(mutexee_cond_t * cond);
int mutexee_cond_destroy(mutexee_cond_t * cond);
void mutexee_thread_start(void);
void mutexee_thread_exit(void);
void mutexee_application_init(void);
void mutexee_application_exit(void);

typedef mutexee_mutex_t lock_mutex_t;
typedef mutexee_context_t lock_context_t;
typedef mutexee_cond_t lock_cond_t;

#define lock_mutex_create mutexee_mutex_create
#define lock_mutex_lock mutexee_mutex_lock
#define lock_mutex_trylock mutexee_mutex_trylock
#define lock_mutex_unlock mutexee_mutex_unlock
#define lock_mutex_destroy mutexee_mutex_destroy
#define lock_cond_init mutexee_cond_init
#define lock_cond_timedwait mutexee_cond_timedwait
#define lock_cond_wait mutexee_cond_wait
#define lock_cond_signal mutexee_cond_signal
#define lock_cond_broadcast mutexee_cond_broadcast
#define lock_cond_destroy mutexee_cond_destroy
#define lock_thread_start mutexee_thread_start
#define lock_thread_exit mutexee_thread_exit
#define lock_application_init mutexee_application_init
#define lock_application_exit mutexee_application_exit
#define lock_init_context mutexee_init_context

#endif				// __mutexee_H__
