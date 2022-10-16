/*
 * File: mutexee_in.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      The mutexee spin-futex adaptive lock algorithm.
 *
 *      The mutexee is an adaptive spin-futex lock.
 *      Mutexee measures the spin-to-mutex acquire (or release) ratio and adjusts
 *      the spinning behavior of the lock accordingly. For example, when the ratio
 *      is below the target limit, mutexee might try to increase the number of 
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

#ifndef _MUTEXEE_IN_H_
#define _MUTEXEE_IN_H_

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
#include <mutexee.h>

#include "waiting_policy.h"
#include "interpose.h"
#include "utils.h"

static inline int
sys_futex(void *addr1, int op, int val1, struct timespec *timeout, void *addr2,
	  int val3)
{
	return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

mutexee_mutex_t *mutexee_mutex_create(const pthread_mutexattr_t * attr)
{
	mutexee_mutex_t *impl =
	    (mutexee_mutex_t *) alloc_cache_align(sizeof(mutexee_mutex_t));
	impl->l.u = 0;
	impl->n_spins = MUTEXEE_SPIN_TRIES_LOCK_MIN;
	impl->n_spins_unlock = MUTEXEE_SPIN_TRIES_UNLOCK_MIN;
	impl->n_acq = 0;
	impl->n_miss = 0;
	impl->n_miss_limit = MUTEXEE_FUTEX_LIM;
	impl->is_futex = 0;
	impl->n_acq_first_sleep = 0;
	impl->retry_spin_every = MUTEXEE_RETRY_SPIN_EVERY;
	return impl;
}

int mutexee_mutex_destroy(mutexee_mutex_t * m)
{
	/* Do nothing */
	(void)m;
	return 0;
}

#define __mutexee_unlikely(x) __builtin_expect((x), 0)

static inline uint64_t mutexee_getticks(void)
{
	unsigned hi, lo;
	__asm__ __volatile__("rdtsc":"=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

#define MUTEXEE_FOR_N_CYCLES(n, do)		\
  {						\
  uint64_t ___s = mutexee_getticks();		\
  while (1)					\
    {						\
      do;					\
      uint64_t ___e = mutexee_getticks();	\
      if ((___e - ___s) > n)			\
	{					\
	  break;				\
	}					\
    }						\
  }
const struct timespec mutexee_max_sleep = {.tv_sec = MUTEXEE_FTIMEOUTS,
	.tv_nsec = MUTEXEE_FTIMEOUT
};

int mutexee_mutex_lock(mutexee_mutex_t * m, mutexee_context_t * me)
{
	if (!xchg_8(&m->l.b.locked, 1)) {
		return 0;
	}
#if MUTEXEE_DO_ADAP == 1
	const register unsigned int time_spin = m->n_spins;
#else
	const unsigned int time_spin = MUTEXEE_SPIN_TRIES_LOCK;
#endif
	MUTEXEE_FOR_N_CYCLES(time_spin, if (!xchg_8(&m->l.b.locked, 1)) {
			     return 0;}
			     asm volatile ("mfence");) ;

	/* Have to sleep */
#if MUTEXEE_FAIR > 0
	int once = 1;
	while (xchg_32(&m->l.u, 257) & 1) {
		asm volatile ("mfence");
		if (once) {
			int ret = sys_futex(m, FUTEX_WAIT_PRIVATE, 257,
					    (struct timespec *)
					    &mutexee_max_sleep, NULL, 0);
			if (ret == -1 && errno == ETIMEDOUT) {
				once = 0;
			}
		}
	}
#else				/* not fair */
	while (xchg_32(&m->l.u, 257) & 1) {
		sys_futex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
	}
#endif				/* MUTEXEE_FAIR */

	return 0;
}

static inline void mutexee_mutex_training(mutexee_mutex_t * m)
{
	const size_t n_acq_curr = ++m->n_acq;
	if (__mutexee_unlikely((n_acq_curr & MUTEXEE_ADAP_EVERY) == 0)) {
		if (!m->is_futex) {
			if (m->n_miss > m->n_miss_limit) {
#if MUTEXEE_PRINT == 1
				printf
				    ("[MUTEXEE] n_miss = %d  > %d :: switch to mutex\n",
				     m->n_miss, m->n_miss_limit);
#endif
				m->n_spins = MUTEXEE_SPIN_TRIES_LOCK_MIN;
				m->n_spins_unlock =
				    MUTEXEE_SPIN_TRIES_UNLOCK_MIN;
				m->is_futex = 1;
			}
		} else {
			unsigned int re = m->retry_spin_every;
			if (m->is_futex++ == re) {
				if (re < MUTEXEE_RETRY_SPIN_EVERY_MAX) {
					re <<= 1;
				}
				m->retry_spin_every = re;
				/* m->n_miss_limit++; */
				if (m->n_miss_limit < MUTEXEE_FUTEX_LIM_MAX) {
					m->n_miss_limit++;
				}
				m->is_futex = 0;
#if MUTEXEE_PRINT == 1
				printf("[MUTEXEE] TRY :: switch to spinlock\n");
#endif
				m->n_spins = MUTEXEE_SPIN_TRIES_LOCK;
				m->n_spins_unlock = MUTEXEE_SPIN_TRIES_UNLOCK;
			}
		}
		m->n_miss = 0;
	}
}

int mutexee_mutex_unlock(mutexee_mutex_t * m, mutexee_context_t * me)
{
	/* Locked and not contended */
	if ((m->l.u == 1) && (mutexee_cas(&m->l.u, 1, 0) == 1)) {
		return 0;
	}

	MUTEXEE_ADAP(mutexee_mutex_training(m);
	    );

	/* Unlock */
	m->l.b.locked = 0;
	asm volatile ("mfence");

	if (m->l.b.locked) {
		return 0;
	}

	asm volatile ("mfence");
#if MUTEXEE_ADAP == 1
	mutexee_cdelay(m->n_spins_unlock);
#else
	mutexee_cdelay(MUTEXEE_SPIN_TRIES_UNLOCK);
#endif
	asm volatile ("mfence");
	if (m->l.b.locked) {
		return 0;
	}

	/* We need to wake someone up */
	m->l.b.contended = 0;

	MUTEXEE_ADAP(m->n_miss++;
	    );
	sys_futex(m, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
	return 0;
}

static inline int
mutexee_mutex_trylock(mutexee_mutex_t * m, mutexee_context_t * me)
{
	unsigned c = xchg_8(&m->l.b.locked, 1);
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
	(void)a;

	c->m = NULL;

	/* Sequence variable doesn't actually matter, but keep valgrind happy */
	c->seq = 0;

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
	/* We are waking someone up */
	atomic_add(&c->seq, 1);

	/* Wake up a thread */
	sys_futex(&c->seq, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

	return 0;
}

static inline int upmutex_cond1_broadcast(upmutex_cond1_t * c)
{
	mutexee_mutex_t *m = c->m;

	/* No mutex means that there are no waiters */
	if (!m)
		return 0;

	/* We are waking everyone up */
	atomic_add(&c->seq, 1);

	/* Wake one thread, and requeue the rest on the mutex */
	sys_futex(&c->seq, FUTEX_REQUEUE_PRIVATE, 1, (struct timespec *)INT_MAX,
		  m, 0);

	return 0;
}

static inline int upmutex_cond1_wait(upmutex_cond1_t * c, mutexee_mutex_t * m)
{
	// int seq = c->seq;

	// if (c->m != m)
	//   {
	// if (c->m) return EINVAL;
	// /* Atomically set mutex inside cv */
	// __attribute__ ((unused)) int dummy = (uintptr_t) mutexee_cas(&c->m, NULL, m);
	// if (c->m != m) return EINVAL;
	//   }

	// mutexee_mutex_unlock(m);

	// sys_futex(&c->seq, FUTEX_WAIT_PRIVATE, seq, NULL, NULL, 0);

	// while (xchg_32(&m->l.b.locked, 257) & 1)
	//   {
	// sys_futex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
	//   }

	return 0;
}

void mutexee_application_init(void)
{

}

void mutexee_application_exit(void)
{

}

void mutexee_thread_start(void)
{

}

void mutexee_thread_exit(void)
{

}

int mutexee_cond_timedwait(mutexee_cond_t * cond,
			   mutexee_mutex_t * lock,
			   mutexee_context_t * me, const struct timespec *ts)
{

	return 0;
}

static inline int
mutexee_mutex_timedlock(mutexee_mutex_t * l, const struct timespec *ts)
{
	fprintf(stderr,
		"** warning -- pthread_mutex_timedlock not implemented\n");
	return 0;
}

#endif
