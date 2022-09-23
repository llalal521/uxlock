#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <cst.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <papi.h>

#include "cst.h"
#include "interpose.h"
#include "waiting_policy.h"
#include "utils.h"

/* Default Number */
#define NO_UX_MAX_WAIT_TIME     100000000
#define SHORT_BATCH_THRESHOLD   65533	/* Should less than 2^16 65536 */
#define ADJUST_THRESHOLD	1
#define ADJUST_FREQ		100	/* Should less than SHORT_BATCH_THRESHOLD */
#define DEFAULT_SHORT_THRESHOLD	1000
#define MAX_CS_LEN		10000000

#define NODE_ACTIVE 1
#define NODE_SLEEP 0

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
		//// // // // printf("Loc Stack FULL!\n");
		return -1;
	}
	loc_stack[stack_pos] = loc;
	return 0;
}

static uint64_t __always_inline rdtscp(void)
{
	uint32_t a, d;
	__asm __volatile("rdtscp; mov %%eax, %0; mov %%edx, %1; cpuid"
			 : "=r" (a), "=r" (d)
			 : : "%rax", "%rbx", "%rcx", "%rdx");
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}

/* Per-thread private stack */
int pop_loc(void)
{
	if (stack_pos < 0)
		return -1;	/* Return -1, give it to cur_loc. */
	return loc_stack[stack_pos--];
}

/* Helper functions */
void *cst_alloc_cache_align(size_t n)
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

cst_mutex_t *cst_mutex_create(const pthread_mutexattr_t * attr)
{
	cst_mutex_t *impl =
	    (cst_mutex_t *) cst_alloc_cache_align(sizeof(cst_mutex_t));
	impl->tail = 0;
	return impl;
}

static int __cst_mutex_trylock(cst_mutex_t * impl, cst_node_t * me)
{
	cst_node_t *expected;
	assert(me != NULL);
	me->next = NULL;
	expected = NULL;
	return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
					   __ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

static void waiting_queue(cst_mutex_t * impl, cst_node_t * me)
{
	while(me) {
		if(me->wait == NODE_SLEEP) {
			me = me->next;
			continue;
		}	

		__atomic_store_n(&me->wait, NODE_SLEEP, __ATOMIC_RELEASE);

		me = me->next;
	}
}

static void waking_queue(cst_mutex_t * impl, cst_node_t * me)
{
	while(me) {
		if(me->wait == NODE_ACTIVE) {
			me = me->next;
			continue;
		}	
		printf("wake %d\n", me->tid);
		waiting_policy_wake(&me->wait);

		me = me->next;
	}
}

static void __cst_mutex_unlock(cst_mutex_t * impl, cst_node_t * me)
{
	cst_node_t *succ, *next = me->next, *expected;
	uint64_t spin;
	uint64_t batch = (me->spin >> 48) & 0xFFFF;
	cst_node_t *prevHead = (cst_node_t *) (me->spin & 0xFFFFFFFFFFFF);
	cst_node_t *secHead, *secTail, *cur;

	int find = 0;
	// printf("tid %d unlock 1\n", me->tid);

	if (!next) {
		if (!prevHead) {
			expected = me;
			if (__atomic_compare_exchange_n
			    (&impl->tail, &expected, 0, 0, __ATOMIC_RELEASE,
			     __ATOMIC_RELAXED)) {
				goto out;
			}
		} else {
			expected = me;
			if (__atomic_compare_exchange_n
			    (&impl->tail, &expected, prevHead->secTail, 0,
			     __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
					// waking_queue(impl, prevHead);
				__atomic_store_n(&prevHead->spin,
						 0x1000000000000,
						 __ATOMIC_RELEASE);
				printf("prevHead->tid %d should lock %d\n", prevHead->tid, prevHead->wait);
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
	if(prevHead && prevHead->wait == NODE_ACTIVE) {
		prevHead->secTail->next = next;
		__atomic_store_n(&prevHead->spin,
				0x1000000000000,
				 __ATOMIC_RELEASE);
		printf("prevHead->tid %d should lock %d\n", prevHead->tid, prevHead->wait);
		goto out;
	}

	succ = NULL;
	if (batch < SHORT_BATCH_THRESHOLD) {
		/* Find next short CS */
		if (next->wait == NODE_ACTIVE) {
			find = 1;
			cur = next;
			goto find_out;
		}
		secHead = next;
		secTail = next;
		cur = next->next;
		while (cur) {
			if (cur->wait == NODE_ACTIVE) {
				if (prevHead) {
					prevHead->secTail->next = secHead;
				}
				else {
					prevHead = secHead;
				}
				secTail->next = NULL;
				prevHead->secTail = secTail;
				printf("ddddddddddd\n");
				waking_queue(impl, secHead);
				find = 1;
				break;
			}
			secTail = cur;
			cur = cur->next;
		}
 find_out:
		if (find) {
			spin = (uint64_t) prevHead | ((batch + 1) << 48);	/* batch + 1 */
			/* Important: spin should not be 0 */
			/* Release barrier */
			__atomic_store_n(&cur->spin, spin, __ATOMIC_RELEASE);
			printf("cur->tid %d should lock %d %lu\n", cur->tid, cur->wait, cur->spin);
			goto out;
		} 
	}

	/* Not find anything or batch */
	if (prevHead) {;
		prevHead->secTail->next = me->next;
		spin = 0x1000000000000;	/* batch = 1 */
		/* Release barrier */
		waking_queue(impl, prevHead);
		__atomic_store_n(&prevHead->spin, spin, __ATOMIC_RELEASE);
		printf("prevHead->tid %d should lock %d\n", prevHead->tid, prevHead->wait);
		
	} else {
		succ = me->next;
		spin = 0x1000000000000;	/* batch = 1 */
		/* Release barrier after */
		__atomic_store_n(&succ->spin, spin, __ATOMIC_RELEASE);
		printf("succ->tid %d should lock %d\n", succ->tid, succ->wait);
	}
 out:
	// printf("tid %d unlock out\n", me->tid);
	nested_level--;		/* Important! reduce nested level *after* release the lock */
}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __cst_lock_ux(cst_mutex_t * impl, cst_node_t * me, uint64_t timestamp)
{
	cst_node_t *tail;
	int count = 0;
	me->next = NULL;
	me->spin = 0;
	me->tid = cur_thread_id;
	me->wait = NODE_ACTIVE;

	tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
	if (tail) {
		__atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
		while (me->spin == 0) {
			CPU_PAUSE();
			// sched_yield();
            if(count++ == 1000 && rdtscp() - timestamp > 1000) {
				count = 0;
				me->wait = NODE_SLEEP;
				printf("tid %d is here %d sleep %lu \n", me->tid, me->wait, me->spin);
				waiting_policy_sleep(&me->wait);	
				printf("tid %d is here %d sleep %lu out \n", me->tid, me->wait, me->spin);
				timestamp = rdtscp();
			}
		}		
	} else {
		/* set batch to 0 */
		me->spin = 0;
	}
	MEMORY_BARRIER();
	printf("tid %d lock succ %d\n", me->tid, me->wait);
	return 0;
}

/* not-ux-thread reorder if queue not empty */
static inline int __cst_lock_nonux(cst_mutex_t * impl, cst_node_t * me)
{
	uint64_t reorder_window_ddl;
	uint32_t cnt = 0;
	uint64_t current_ns;
	uint64_t sleep_time = 10000;
	if (cst_mutex_trylock(impl, me) == 0)
            return 0;
	/* Someone is holding the lock */
	reorder_window_ddl = get_current_ns() + NO_UX_MAX_WAIT_TIME;
	while (current_ns = get_current_ns() < reorder_window_ddl) {
        sleep_time = sleep_time < reorder_window_ddl - current_ns ?
            sleep_time : reorder_window_ddl - current_ns;
        nanosleep((const struct timespec[]){{0, sleep_time}}, NULL);
        if (cst_mutex_trylock(impl, me) == 0)
            return 0;
        sleep_time = sleep_time << 5;
	}
 out:
	return __cst_lock_ux(impl, me, rdtscp());
}

/* Entry Point: length  */
static int __cst_mutex_lock(cst_mutex_t * impl, cst_node_t * me)
{
	int ret;
	if (uxthread || nested_level > 1)
		return __cst_lock_ux(impl, me, rdtscp());
	else
		return __cst_lock_nonux(impl, me);
}

/* lock function with perdict critical (Do not support litl, use llvm instead) */
int cst_mutex_lock_cri(cst_mutex_t * impl, cst_node_t * me, int loc)
{
	nested_level++;		/* Per-thread nest level cnter, add before hold the lock */

	int ret = __cst_mutex_lock(impl, me);
	/* Critical Section Start */
	tt_start[loc] = PAPI_get_real_cyc();
	if (cur_loc >= 0)
		push_loc(cur_loc);
	cur_loc = loc;		/* No need to read stack in critical path */
	return ret;
}

/* lock function orignal*/
int cst_mutex_lock(cst_mutex_t * impl, cst_node_t * me)
{
	return cst_mutex_lock_cri(impl, me, 0);	/* Default loc 0 */
}

int cst_mutex_trylock(cst_mutex_t * impl, cst_node_t * me)
{

	if (!__cst_mutex_trylock(impl, me)) {
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
void cst_mutex_unlock(cst_mutex_t * impl, cst_node_t * me)
{
	uint64_t duration, end_ts;
	/* Record CS len */
	end_ts = PAPI_get_real_cyc();
	__cst_mutex_unlock(impl, me);
	duration = end_ts - tt_start[cur_loc];
	/* update critical_len */
	if (critical_len[cur_loc] == 0)
		critical_len[cur_loc] = duration;
	else if (duration < MAX_CS_LEN)	/* Not too long to record */
		critical_len[cur_loc] =
		    ((critical_len[cur_loc] * 7) + duration) >> 3;
	cur_loc = pop_loc();
	// // // // // printf("me->tid %d unlock\n", me->tid);
}

int cst_mutex_destroy(cst_mutex_t * lock)
{
#if COND_VAR
	REAL(pthread_mutex_destroy)
	    (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

int cst_cond_init(cst_cond_t * cond, const pthread_condattr_t * attr)
{
	return 0;
}

int cst_cond_timedwait(cst_cond_t * cond, cst_mutex_t * lock,
		       cst_node_t * me, const struct timespec *ts)
{
	return 0;
}

int cst_cond_wait(cst_cond_t * cond, cst_mutex_t * lock, cst_node_t * me)
{
	return cst_cond_timedwait(cond, lock, me, 0);
}

int cst_cond_signal(cst_cond_t * cond)
{
	return 0;
}

void cst_thread_start(void)
{
}

void cst_thread_exit(void)
{
}

void cst_application_init(void)
{
}

void cst_application_exit(void)
{
}

int cst_cond_broadcast(cst_cond_t * cond)
{
	return 0;
}

int cst_cond_destroy(cst_cond_t * cond)
{
	return 0;
}

void cst_init_context(lock_mutex_t * UNUSED(impl),
		      lock_context_t * UNUSED(context), int UNUSED(number))
{
}

/* New interfaces in Libcst */
/* set a thread is uxthread or not */
void set_ux(int is_ux)
{
	uxthread = is_ux;
}

