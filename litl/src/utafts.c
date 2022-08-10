#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <utafts.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <papi.h>

#include "utafts.h"
#include "interpose.h"
#include "utils.h"

/* Default Number */
#define NO_UX_MAX_WAIT_TIME     10000000000
#define ADJUST_THRESHOLD	1
#define ADJUST_FREQ		100	/* Should less than SHORT_BATCH_THRESHOLD */
#define DEFAULT_SHORT_THRESHOLD	10000
#define MAX_CS_LEN		10000000
#define DEAFULT_REFILL_WINDOW	1000	/* Execute window of each core */

#define NOT_UX_THREAD 0
#define IS_UX_THREAD 1
__thread int nested_level = 0;
__thread unsigned int uxthread = NOT_UX_THREAD;
extern __thread unsigned int cur_thread_id;

/* Helper functions */
void *utafts_alloc_cache_align(size_t n)
{
	void *res = 0;
	if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res) {
		fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
			(unsigned long long)cache_align(n));
		exit(-1);
	}
	return res;
}

uint64_t get_current_ns(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000000000LL + now.tv_nsec;
}

utafts_mutex_t *utafts_mutex_create(const pthread_mutexattr_t * attr)
{
	utafts_mutex_t *impl =
	    (utafts_mutex_t *) utafts_alloc_cache_align(sizeof(utafts_mutex_t));
	impl->tail = 0;
	impl->refill_window = DEAFULT_REFILL_WINDOW;
	return impl;
}

static int __utafts_mutex_trylock(utafts_mutex_t * impl, utafts_node_t * me)
{
	utafts_node_t *expected;
	assert(me != NULL);
	me->next = NULL;
	expected = NULL;
	return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
					   __ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

static void __utafts_mutex_unlock(utafts_mutex_t * impl, utafts_node_t * me)
{
	utafts_node_t *succ, *next = me->next;
	utafts_node_t *prevHead = (utafts_node_t *) (me->spin & 0xFFFFFFFFFFFF);
	utafts_node_t *secHead, *secTail, *cur;
	uint64_t spin;
	int find = 0;
	uint64_t refill_window = impl->refill_window;

	if (!next) {
		if (!prevHead) {
			if (__sync_val_compare_and_swap(&impl->tail, me, NULL)
			    == me) {
				// printf("No comp.!\n");
				me->remain_window = refill_window;
				goto out;
			}
		} else {
			if (__sync_val_compare_and_swap
			    (&impl->tail, me, prevHead->secTail) == me) {
				/* Refill the window */
				prevHead->remain_window = impl->refill_window;
				__atomic_store_n(&prevHead->spin,
						 0x1000000000000,
						 __ATOMIC_RELEASE);
				// printf("Second queue %p\n", prevHead);
				goto out;
			}
		}
		while (!(next = me->next))
			CPU_PAUSE();
	}

	/*
	 * Determine the next lock holder and pass the lock by
	 * setting its spin field
	 */
	succ = NULL;
	/* Find next competitor with remain window, fast out */
	if (next->remain_window > 0) {
		find = 1;
		cur = next;
		goto find_out;
	}
	secHead = next;
	secTail = next;
	next->remain_window = refill_window;
	cur = next->next;
	while (cur) {
		if (cur->remain_window > 0) {
			if (prevHead)
				prevHead->secTail->next = secHead;
			else
				prevHead = secHead;
			secTail->next = NULL;
			prevHead->secTail = secTail;
			find = 1;
			break;
		} else {
			/* Refill remain_window, put into secondary queue */
			cur->remain_window = refill_window;
			// printf("refill %d\n", cur->id);
		}
		secTail = cur;
		cur = cur->next;
	}
 find_out:
	if (find) {
		spin = (uint64_t) prevHead | 0x1000000000000;
		/* Release barrier */
		__atomic_store_n(&cur->spin, spin, __ATOMIC_RELEASE);
		// printf("Relase to active queue %p %p\n", cur, prevHead);
		goto out;
	}

	/* Not find anything */
	if (prevHead) {
		prevHead->secTail->next = me->next;
		spin = 0x1000000000000;
		// printf("Release to second %p\n", prevHead);
		/* Release barrier */
		__atomic_store_n(&prevHead->spin, spin, __ATOMIC_RELEASE);
	} else {
		succ = me->next;
		spin = 0x1000000000000;
		// printf("Release to me next %p\n", me->next);
		/* Release barrier after */
		__atomic_store_n(&succ->spin, spin, __ATOMIC_RELEASE);
	}
 out:
	nested_level--;		/* Important! reduce nested level *after* release the lock */

}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __utafts_lock_ux(utafts_mutex_t * impl, utafts_node_t * me)
{
	utafts_node_t *tail;
	me->next = NULL;
	me->spin = 0;
	tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
	if (tail) {
		__atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
		while (me->spin == 0)
			CPU_PAUSE();
	} else {
		/* set batch to 0 */
		me->spin = 0;
	}
	return 0;
}

/* not-ux-thread reorder if queue not empty */
static inline int __utafts_lock_nonux(utafts_mutex_t * impl, utafts_node_t * me)
{
	uint64_t reorder_window_ddl;
	uint32_t cnt = 0;
	if (impl->tail == NULL)
		goto out;
	/* Someone is holding the lock */
	reorder_window_ddl = get_current_ns() + NO_UX_MAX_WAIT_TIME;
	while (get_current_ns() < reorder_window_ddl) {
		/* Spin-check if the queue is empty */
		if (impl->tail == NULL) {
			goto out;
		}
	}
 out:
	return __utafts_lock_ux(impl, me);
}

/* Entry Point: length  */
static int __utafts_mutex_lock(utafts_mutex_t * impl, utafts_node_t * me)
{
	int ret;
	if (uxthread || nested_level > 1)
		return __utafts_lock_ux(impl, me);
	else
		return __utafts_lock_nonux(impl, me);
}

/* lock function orignal*/
int utafts_mutex_lock(utafts_mutex_t * impl, utafts_node_t * me)
{
	nested_level++;		/* Per-thread nest level cnter, add before hold the lock */
	if (nested_level > 1) /* higher prio */
		me->remain_window = me->remain_window < 0 ? 1 : me->remain_window;
	// printf("%p lock remain %d\n", me, me->remain_window);
	// me->id = sched_getcpu(); /* Debug use */
	int ret = __utafts_mutex_lock(impl, me);
	/* Critical Section Start */
	me->start_ts = PAPI_get_real_cyc();
	// printf("%p get lock\n", me);
	return ret;
}

int utafts_mutex_trylock(utafts_mutex_t * impl, utafts_node_t * me)
{

	if (!__utafts_mutex_trylock(impl, me)) {
		nested_level++;	/* Per-thread nest level cnter */
#if COND_VAR
		REAL(pthread_mutex_lock)
		    (&impl->posix_lock);
#endif
		/* Critical Section Start */
		me->start_ts = PAPI_get_real_cyc();
		return 0;
	}
	return -EBUSY;
}

/* unlock function orignal*/
void utafts_mutex_unlock(utafts_mutex_t * impl, utafts_node_t * me)
{
	int64_t duration, end_ts;
	/* Record CS len */
	end_ts = PAPI_get_real_cyc();
	__utafts_mutex_unlock(impl, me);
	duration = end_ts - me->start_ts;
	me->remain_window -= duration;	/* Consume remain window */
	// printf("%d unlock duration %d remain %d\n", me->id, duration, me->remain_window);
	if (impl->refill_window < duration) {
		impl->refill_window = duration << 3;
		// printf("update refill_window to %d\n", impl->refill_window);
	}
}

int utafts_mutex_destroy(utafts_mutex_t * lock)
{
#if COND_VAR
	REAL(pthread_mutex_destroy)
	    (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

int utafts_cond_init(utafts_cond_t * cond, const pthread_condattr_t * attr)
{
	return 0;
}

int utafts_cond_timedwait(utafts_cond_t * cond, utafts_mutex_t * lock,
			  utafts_node_t * me, const struct timespec *ts)
{
	return 0;
}

int utafts_cond_wait(utafts_cond_t * cond, utafts_mutex_t * lock,
		     utafts_node_t * me)
{
	return utafts_cond_timedwait(cond, lock, me, 0);
}

int utafts_cond_signal(utafts_cond_t * cond)
{
	return 0;
}

void utafts_thread_start(void)
{
}

void utafts_thread_exit(void)
{
}

void utafts_application_init(void)
{
}

void utafts_application_exit(void)
{
}

int utafts_cond_broadcast(utafts_cond_t * cond)
{
	return 0;
}

int utafts_cond_destroy(utafts_cond_t * cond)
{
	return 0;
}

void utafts_init_context(lock_mutex_t * UNUSED(impl),
			 lock_context_t * UNUSED(context), int UNUSED(number))
{
}

/* New interfaces in Libutafts */
/* set a thread is uxthread or not */
void set_ux(int is_ux)
{
	uxthread = is_ux;
}
