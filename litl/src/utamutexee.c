/*
 * File: utamutexee_in.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      The utamutexee spin-futex adaptive lock algorithm.
 *
 *      The utamutexee is an adaptive spin-futex lock.
 *      Mutexee measures the spin-to-mutex acquire (or release) ratio and adjusts
 *      the spinning behavior of the lock accordingly. For example, when the ratio
 *      is below the target limit, utamutexee might try to increase the number of 
 *      spins. If this increment is unsucessful, it might decides to increase it 
 *      further, or to simply become a futex-lock. Mutexee eventuall reaches some
 *      stable states, but never stops trying to find a better stable state.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Vasileios Trigonakis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* 
 */

#ifndef _UTAMUTEXEE_IN_H_
#define _UTAMUTEXEE_IN_H_

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <malloc.h>
#include <limits.h>
#include <utamutexee.h>

#include "waiting_policy.h"
#include "interpose.h"
#include "utils.h"

__thread int cur_loc = -1;
__thread int stack_pos = -1;
__thread int nested_level = 0;
#define STACK_SIZE 128
__thread int loc_stack[STACK_SIZE];
#define MAX_LOC 128
#define MAX_THREAD 128
#define MAX_CS_LEN		10000000
__thread uint64_t tt_start[MAX_THREAD], critical_len;
extern __thread  int cur_thread_id;
int cnt[MAX_LOC] = { 0 };

/* Per-thread private stack, avoid nest lock cover loc_stack*/
int push_loc(int loc)
{
	stack_pos++;
	if (stack_pos == STACK_SIZE) {
		printf("Loc Stack FULL!\n");
		return -1;
	}
	loc_stack[stack_pos] = loc;
	return 0;
}

/* Per-thread private stack */
int pop_loc(void)
{
	if (stack_pos < 0)
		return -1;	/* Return -1, give it to cur_loc. */
	return loc_stack[stack_pos--];
}

static inline int
sys_futex(void *addr1, int op, int val1, struct timespec *timeout, void *addr2,
	  int val3)
{
	return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

utamutexee_mutex_t *utamutexee_mutex_create(const pthread_mutexattr_t * attr)
{
	utamutexee_mutex_t *impl =
	    (utamutexee_mutex_t *)
	    alloc_cache_align(sizeof(utamutexee_mutex_t));
	impl->l.u = 0;
	impl->n_spins = UTAMUTEXEE_SPIN_TRIES_LOCK;
	return impl;
}

int utamutexee_mutex_destroy(utamutexee_mutex_t * impl)
{
	/* Do nothing */
	(void)impl;
	return 0;
}

#define __utamutexee_unlikely(x) __builtin_expect((x), 0)

static inline uint64_t utamutexee_getticks(void)
{
	unsigned hi, lo;
	__asm__ __volatile__("rdtsc":"=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

#define UTAMUTEXEE_FOR_N_CYCLES(n, do)		\
  {						\
  uint64_t ___s = utamutexee_getticks();		\
  while (1)					\
    {						\
      do;					\
      uint64_t ___e = utamutexee_getticks();	\
      if ((___e - ___s) > n)			\
	{					\
	  break;				\
	}					\
    }						\
  }
const struct timespec utamutexee_max_sleep = {.tv_sec = UTAMUTEXEE_FTIMEOUTS,
	.tv_nsec = UTAMUTEXEE_FTIMEOUT
};
int __utamutexee_mutex_lock(utamutexee_mutex_t * impl, utamutexee_context_t * me)
{
	if (!xchg_8(&impl->l.b.locked, 1)) {
		return 0;
	}
	int len = impl->cri_len;
	unsigned int time_spin;
	if(len > 1000) {
		// printf("111\n");
		time_spin = len;
	}
	else if(len > 500)
		time_spin = len;
	else if(len > 250){
		time_spin = len;
	}
	else 
		time_spin = len;
	// else {
		// printf("2222\n");
		// time_spin = 8192;
	// }
		
	// else
	// 	time_spin = len * 4;
	// if(impl->cri_len < 10000)
	// 	time_spin = 2048;
	// else if(impl->cri_len < 800)
	// 	time_spin = 2048;
	// else if(impl->cri_len < 1500)
	// 	time_spin = 2048;
	// else
	// 	time_spin = 8192;
	// const unsigned int time_spin = impl->cri_len * 4;
	UTAMUTEXEE_FOR_N_CYCLES(time_spin, if (!xchg_8(&impl->l.b.locked, 1)) {
				return 0;}
				asm volatile ("mfence");) ;

	/* Have to sleep */
#if UTAMUTEXEE_FAIR > 0
	int once = 1;
	while (xchg_32(&impl->l.u, 257) & 1) {
		asm volatile ("mfence");
		if (once) {
			int ret = sys_futex(impl, FUTEX_WAIT_PRIVATE, 257,
					    (struct timespec *)
					    &utamutexee_max_sleep, NULL, 0);
			if (ret == -1 && errno == ETIMEDOUT) {
				once = 0;
			}
		}
	}
#else				/* not fair */
	while (xchg_32(&impl->l.u, 257) & 1) {
		sys_futex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
	}
#endif				/* UTAMUTEXEE_FAIR */

	return 0;
}
int utamutexee_mutex_lock(utamutexee_mutex_t * impl, utamutexee_context_t * me)
{
	// impl->cri_len = critical_len;
	int ret = __utamutexee_mutex_lock(impl, me);
	tt_start[cur_thread_id] = PAPI_get_real_cyc();
	return ret;
}

int __utamutexee_mutex_unlock(utamutexee_mutex_t * impl, utamutexee_context_t * me)
{
	/* Locked and not contended */
	if ((impl->l.u == 1) && (utamutexee_cas(&impl->l.u, 1, 0) == 1)) {
		return 0;
	}

	/* Unlock */
	impl->l.b.locked = 0;
	asm volatile ("mfence");

	if (impl->l.b.locked) {
		return 0;
	}

	asm volatile ("mfence");
	utamutexee_cdelay(UTAMUTEXEE_SPIN_TRIES_UNLOCK);
	asm volatile ("mfence");
	if (impl->l.b.locked) {
		return 0;
	}

	/* We need to wake someone up */
	impl->l.b.contended = 0;

	sys_futex(impl, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
	return 0;
}
int utamutexee_mutex_unlock(utamutexee_mutex_t * impl, utamutexee_context_t * me)
{
	uint64_t duration, end_ts;
	end_ts = PAPI_get_real_cyc();
	int ret = __utamutexee_mutex_unlock(impl, me);
	duration = end_ts - tt_start[cur_thread_id];
	/* update critical_len */
	if (critical_len == 0)
		critical_len = duration;
	else if (duration < MAX_CS_LEN)	/* Not too long to record */
		critical_len =
		    ((critical_len * 7) + duration) >> 3;
	impl->cri_len = critical_len;
	// printf("now critical_len %d\n", critical_len);
	return ret;
}

static inline int
utamutexee_mutex_trylock(utamutexee_mutex_t * impl, utamutexee_context_t * me)
{
	unsigned c = xchg_8(&impl->l.b.locked, 1);
	if (!c)
		return 0;
	return EBUSY;
}

  /* ******************************************************************************** */
  /* condition variables */
  /* ******************************************************************************** */

static inline int
upmutex_cond1_init(upmutex_cond1_t * c, const pthread_condattr_t * a)
{
	return 0;
}

static inline int upmutex_cond1_destroy(upmutex_cond1_t * c)
{
	/* No need to do anything */
	(void)c;
	return 0;
}

static inline int upmutex_cond1_signal(upmutex_cond1_t * c)
{
	return 0;
}

static inline int upmutex_cond1_broadcast(upmutex_cond1_t * c)
{
	return 0;
}

static inline int
upmutex_cond1_wait(upmutex_cond1_t * c, utamutexee_mutex_t * m)
{

	return 0;
}

void utamutexee_application_init(void)
{

}

void utamutexee_application_exit(void)
{

}

void utamutexee_thread_start(void)
{

}

void utamutexee_thread_exit(void)
{

}

int utamutexee_cond_timedwait(utamutexee_cond_t * cond,
			      utamutexee_mutex_t * lock,
			      utamutexee_context_t * me,
			      const struct timespec *ts)
{

	return 0;
}

static inline int
utamutexee_mutex_timedlock(utamutexee_mutex_t * l, const struct timespec *ts)
{
	fprintf(stderr,
		"** warning -- pthread_mutex_timedlock not implemented\n");
	return 0;
}

#endif
