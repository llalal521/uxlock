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
#ifndef __utamutexee_H__
#define __utamutexee_H__

#include "padding.h"
#define LOCK_ALGORITHM "UTAMUTEXEE"
#define NEED_CONTEXT 0
#define SUPPORT_WAITING 0

  /* ******************************************************************************** */
  /* settings *********************************************************************** */
#define UTAMUTEXEE_SPIN_TRIES_LOCK       2048	/* spinning retries before futex */
#define UTAMUTEXEE_SPIN_TRIES_UNLOCK     384	/* spinning retries before futex wake */

#define UTAMUTEXEE_PRINT               0	/* print debug output  */

#define UTAMUTEXEE_FAIR              1	/* enable or not mechanisms for capping
					   the maximum tail latency of the lock */

#define UTAMUTEXEE_FTIMEOUTS 0	/* timeout seconds */
#define UTAMUTEXEE_FTIMEOUT   30000000	/* timeout nanoseconds - max 1e9-1
					   if you want to set it to more than 1 sec
					   use UTAMUTEXEE_FTIMEOUTS */
  /* ******************************************************************************** */

#if defined(PAUSE_IN)
#undef PAUSE_IN
#define PAUSE_IN()				\
  asm volatile ("mfence");
#endif

static inline void utamutexee_cdelay(const int cycles)
{
	int cy = cycles;
	while (cy--) {
		asm volatile ("");
	}
}

  //Swap uint32_t
static inline uint32_t
utamutexee_swap_uint32(volatile uint32_t * target, uint32_t x)
{
	asm volatile ("xchgl %0,%1":"=r" ((uint32_t) x)
		      :"m"(*(volatile uint32_t *)target), "0"((uint32_t) x)
		      :"memory");

	return x;
}

  //Swap uint8_t
static inline uint8_t
utamutexee_swap_uint8(volatile uint8_t * target, uint8_t x)
{
	asm volatile ("xchgb %0,%1":"=r" ((uint8_t) x)
		      :"m"(*(volatile uint8_t *)target), "0"((uint8_t) x)
		      :"memory");

	return x;
}

#define utamutexee_cas(a, b, c) __sync_val_compare_and_swap(a, b, c)
#define xchg_32(a, b)    utamutexee_swap_uint32((uint32_t*) a, b)
#define xchg_8(a, b)     utamutexee_swap_uint8((uint8_t*) a, b)
#define atomic_add(a, b) __sync_fetch_and_add(a, b)

#define CACHE_LINE_SIZE 64

typedef __attribute__((aligned(CACHE_LINE_SIZE)))
struct utamutexee_lock {
	union {
		volatile unsigned u;
		struct {
			volatile unsigned char locked;
			volatile unsigned char contended;
		} b;
	} l;
 	uint8_t padding[4];
	unsigned int n_spins;
	unsigned int cri_len;
	/* uint8_t padding_cl[56]; */
	uint8_t padding2[CACHE_LINE_SIZE - 3 * sizeof(unsigned)];
} utamutexee_mutex_t;

typedef struct upmutex_cond1 {
	utamutexee_mutex_t *m;
	int seq;
	int pad;
#if PADDING == 1
	uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} upmutex_cond1_t;

typedef pthread_cond_t utamutexee_cond_t;
#define UPMUTEX_COND1_INITIALIZER {NULL, 0, 0}
typedef utamutexee_cond_t utamutexee_cond_t;
typedef void *utamutexee_context_t;	// Unused, take the less space
					   // as possible

utamutexee_mutex_t *utamutexee_mutex_create(const pthread_mutexattr_t * attr);
int utamutexee_mutex_lock(utamutexee_mutex_t * impl, utamutexee_context_t * me);
static inline int utamutexee_mutex_trylock(utamutexee_mutex_t * impl,
					   utamutexee_context_t * me);
int utamutexee_mutex_unlock(utamutexee_mutex_t * impl,
			    utamutexee_context_t * me);
int utamutexee_mutex_destroy(utamutexee_mutex_t * lock);
int utamutexee_cond_init(utamutexee_cond_t * cond,
			 const pthread_condattr_t * attr);
int utamutexee_cond_timedwait(utamutexee_cond_t * cond,
			      utamutexee_mutex_t * lock,
			      utamutexee_context_t * me,
			      const struct timespec *ts);
int utamutexee_cond_wait(utamutexee_cond_t * cond,
			 utamutexee_mutex_t * lock, utamutexee_context_t * me);
int utamutexee_cond_signal(utamutexee_cond_t * cond);
int utamutexee_cond_broadcast(utamutexee_cond_t * cond);
int utamutexee_cond_destroy(utamutexee_cond_t * cond);
void utamutexee_thread_start(void);
void utamutexee_thread_exit(void);
void utamutexee_application_init(void);
void utamutexee_application_exit(void);

typedef utamutexee_mutex_t lock_mutex_t;
typedef utamutexee_context_t lock_context_t;
typedef utamutexee_cond_t lock_cond_t;

#define lock_mutex_create utamutexee_mutex_create
#define lock_mutex_lock utamutexee_mutex_lock
#define lock_mutex_trylock utamutexee_mutex_trylock
#define lock_mutex_unlock utamutexee_mutex_unlock
#define lock_mutex_destroy utamutexee_mutex_destroy
#define lock_cond_init utamutexee_cond_init
#define lock_cond_timedwait utamutexee_cond_timedwait
#define lock_cond_wait utamutexee_cond_wait
#define lock_cond_signal utamutexee_cond_signal
#define lock_cond_broadcast utamutexee_cond_broadcast
#define lock_cond_destroy utamutexee_cond_destroy
#define lock_thread_start utamutexee_thread_start
#define lock_thread_exit utamutexee_thread_exit
#define lock_application_init utamutexee_application_init
#define lock_application_exit utamutexee_application_exit
#define lock_init_context utamutexee_init_context

#endif				// __utamutexee_H__
