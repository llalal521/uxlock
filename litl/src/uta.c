#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <uta.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <papi.h>

#include "uta.h"
#include "interpose.h"
#include "utils.h"

/* Default Number */
#define NO_UX_MAX_WAIT_TIME     10000000000
#define SHORT_BATCH_THRESHOLD   65534 /* Should less than 2^16 65536 */
#define ADJUST_THRESHOLD	1
#define ADJUST_FREQ		100	/* Should less than SHORT_BATCH_THRESHOLD */
#define DEFAULT_SHORT_THRESHOLD	10000
#define MAX_CS_LEN		10000000

#define NOT_UX_THREAD 0
#define IS_UX_THREAD 1
__thread int nested_level = 0;
__thread unsigned int uxthread = NOT_UX_THREAD;
extern __thread unsigned int cur_thread_id;

__thread int cur_loc = -1;
__thread int stack_pos = -1;
#define STACK_SIZE 128
__thread int loc_stack[STACK_SIZE];

/* Predict CS by location */
#define MAX_LOC 128
__thread uint64_t tt_start[MAX_LOC], critical_len[MAX_LOC];
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

/* Helper functions */
void *uta_alloc_cache_align(size_t n)
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

uta_mutex_t *uta_mutex_create(const pthread_mutexattr_t * attr)
{
	uta_mutex_t *impl =
	    (uta_mutex_t *) uta_alloc_cache_align(sizeof(uta_mutex_t));
	impl->tail = 0;
	impl->threshold = DEFAULT_SHORT_THRESHOLD;
	return impl;
}

static int __uta_mutex_trylock(uta_mutex_t * impl, uta_node_t * me)
{
	uta_node_t *expected;
	assert(me != NULL);
	me->next = NULL;
	expected = NULL;
	return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
					   __ATOMIC_ACQ_REL, __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

static void __uta_mutex_unlock(uta_mutex_t * impl, uta_node_t * me)
{
	uta_node_t *succ, *next = me->next;
	uint64_t spin;
	uint64_t batch = (me->spin >> 48) & 0xFFFF;
	uta_node_t *prevHead = (uta_node_t *) (me->spin & 0xFFFFFFFFFFFF);
	uta_node_t *secHead, *secTail, *cur;
	int32_t threshold = impl->threshold;
	int find = 0;

	if (!next) {
		if (!prevHead) {
			if (__sync_val_compare_and_swap(&impl->tail, me, NULL)
			    == me) {
				goto out;
			}
		} else {
			if (__sync_val_compare_and_swap
			    (&impl->tail, me, prevHead->secTail) == me) {
				__atomic_store_n(&prevHead->spin,
						 0x1000000000000,
						 __ATOMIC_RELEASE);
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
	if (batch < SHORT_BATCH_THRESHOLD) {
		/* Find next short CS */
		if (next->cri_len < threshold) {
			find = 1;
			cur = next;
			goto find_out;
		}
		secHead = next;
		secTail = next;
		cur = next->next;
		while (cur) {
			if (cur->cri_len < threshold) {
				if (prevHead)
					prevHead->secTail->next = secHead;
				else
					prevHead = secHead;
				secTail->next = NULL;
				prevHead->secTail = secTail;
				find = 1;
				break;
			}
			secTail = cur;
			cur = cur->next;
		}
 find_out:
		if (find) {
#ifdef ADJUST_THRESHOLD
			if (batch % ADJUST_FREQ == ADJUST_FREQ - 1) {
				impl->threshold = threshold - 1;
				// printf("decrease threshold %d\n", threshold);
			}
#endif
			spin = (uint64_t) prevHead | ((batch + 1) << 48);	/* batch + 1 */
			/* Release barrier */
			__atomic_store_n(&cur->spin, spin, __ATOMIC_RELEASE);
			goto out;
		} else {
#ifdef ADJUST_THRESHOLD
			impl->threshold = threshold + 1;
			// printf("increase threshold %d\n", threshold);
#endif
		}
	}

	/* Not find anything or batch */
	if (prevHead) {;
		prevHead->secTail->next = me->next;
		spin = 0x1000000000000;	/* batch = 1 */
		/* Release barrier */
		__atomic_store_n(&prevHead->spin, spin, __ATOMIC_RELEASE);
	} else {
		// printf("succ %p %d %d\n", succ, impl->batch, impl->threshold);
		succ = me->next;
		spin = 0x1000000000000;	/* batch = 1 */
		/* Release barrier after */
		__atomic_store_n(&succ->spin, spin, __ATOMIC_RELEASE);
	}
 out:
	nested_level--;		/* Important! reduce nested level *after* release the lock */
}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __uta_lock_ux(uta_mutex_t * impl, uta_node_t * me)
{
	uta_node_t *tail;
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
static inline int __uta_lock_nonux(uta_mutex_t * impl, uta_node_t * me)
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
	return __uta_lock_ux(impl, me);
}

/* Entry Point: length  */
static int __uta_mutex_lock(uta_mutex_t * impl, uta_node_t * me)
{
	int ret;
	if (uxthread || nested_level > 1)
		return __uta_lock_ux(impl, me);
	else
		return __uta_lock_nonux(impl, me);
}

/* lock function with perdict critical (Do not support litl, use llvm instead) */
int uta_mutex_lock_cri(uta_mutex_t * impl, uta_node_t * me, int loc)
{
	me->cri_len = critical_len[loc];
	nested_level++;		/* Per-thread nest level cnter, add before hold the lock */
	if (nested_level > 1)
		me->cri_len = 0;	/* highest prio */
	int ret = __uta_mutex_lock(impl, me);
	/* Critical Section Start */
	tt_start[loc] = PAPI_get_real_cyc();
	if (cur_loc >= 0)
		push_loc(cur_loc);
	cur_loc = loc;		/* No need to read stack in critical path */
	return ret;
}

/* lock function orignal*/
int uta_mutex_lock(uta_mutex_t * impl, uta_node_t * me)
{
	return uta_mutex_lock_cri(impl, me, 0);	/* Default loc 0 */
}

int uta_mutex_trylock(uta_mutex_t * impl, uta_node_t * me)
{

	if (!__uta_mutex_trylock(impl, me)) {
		nested_level++;	/* Per-thread nest level cnter */
#if COND_VAR
		REAL(pthread_mutex_lock)
		    (&impl->posix_lock);
#endif
		return 0;
	}
	return -EBUSY;
}

/* unlock function orignal*/
void uta_mutex_unlock(uta_mutex_t * impl, uta_node_t * me)
{
	int duration;
	/* Record CS len */
	duration = PAPI_get_real_cyc() - tt_start[cur_loc];
	/* update critical_len */
	if (critical_len[cur_loc] == 0)
		critical_len[cur_loc] = duration;
	else if (duration < MAX_CS_LEN)	/* Not too long to record */
		critical_len[cur_loc] =
		    ((critical_len[cur_loc] * 7) + duration) >> 3;
	__uta_mutex_unlock(impl, me);
	cur_loc = pop_loc();	/* Out of critical path */
}

int uta_mutex_destroy(uta_mutex_t * lock)
{
#if COND_VAR
	REAL(pthread_mutex_destroy)
	    (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

int uta_cond_init(uta_cond_t * cond, const pthread_condattr_t * attr)
{
	return 0;
}

int uta_cond_timedwait(uta_cond_t * cond, uta_mutex_t * lock,
			  uta_node_t * me, const struct timespec *ts)
{
	return 0;
}

int uta_cond_wait(uta_cond_t * cond, uta_mutex_t * lock,
		     uta_node_t * me)
{
	return uta_cond_timedwait(cond, lock, me, 0);
}

int uta_cond_signal(uta_cond_t * cond)
{
	return 0;
}

void uta_thread_start(void)
{
}

void uta_thread_exit(void)
{
}

void uta_application_init(void)
{
}

void uta_application_exit(void)
{
}

int uta_cond_broadcast(uta_cond_t * cond)
{
	return 0;
}

int uta_cond_destroy(uta_cond_t * cond)
{
	return 0;
}

void uta_init_context(lock_mutex_t * UNUSED(impl),
			 lock_context_t * UNUSED(context), int UNUSED(number))
{
}

/* New interfaces in Libuta */
/* set a thread is uxthread or not */
void set_ux(int is_ux)
{
	uxthread = is_ux;
}
