#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <utablocking.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <papi.h>

#include "utablocking.h"
#include "interpose.h"
#include "waiting_policy.h"
#include "utils.h"

/* Default Number */
#define NO_UX_MAX_WAIT_TIME     100000000
#define SHORT_BATCH_THRESHOLD   600000	/* Should less than 2^16 65536 */
#define ADJUST_THRESHOLD	1
#define ADJUST_FREQ		100	/* Should less than SHORT_BATCH_THRESHOLD */
#define DEFAULT_SHORT_THRESHOLD	1000
#define MAX_CS_LEN		10000000

#define S_PARKED	0
#define S_READY		1
#define S_ACTIVE	2

#define NOT_UX_THREAD 0
#define IS_UX_THREAD 1
__thread int nested_level = 0;
__thread unsigned int uxthread = NOT_UX_THREAD;
extern __thread unsigned int cur_thread_id;

__thread int cur_loc = -1;
__thread int stack_pos = -1;
#define STACK_SIZE 128
__thread int loc_stack[STACK_SIZE];

unsigned long debug_cnt_0 = 0;
unsigned long debug_cnt_1 = 0;

static inline uint32_t xor_random()
{
	static __thread uint32_t rv = 0;

	if (rv == 0)
		// rv = rand();
		rv = cur_thread_id + 100;

	uint32_t v = rv;
	v ^= v << 6;
	v ^= (uint32_t) (v) >> 21;
	v ^= v << 7;
	rv = v;

	return v;
}

static uint64_t __always_inline rdtscp(void)
{
	uint32_t a, d;
	__asm __volatile("rdtscp; mov %%eax, %0; mov %%edx, %1; cpuid":"=r"(a),
			 "=r"(d)
			 ::"%rax", "%rbx", "%rcx", "%rdx");
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}

static inline int sys_futex(int *uaddr, int op, int val,
			    const struct timespec *timeout, int *uaddr2,
			    int val3)
{
	return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

static inline void park_node(volatile int *var, int target)
{
	int ret = 0;
	while ((ret = sys_futex((int *)var, FUTEX_WAIT_PRIVATE, target, NULL, 0,
				0)) != 0) {
		if (ret == -1 && errno != EINTR) {
			if (errno == EAGAIN) {
				break;
			}
			perror("Unable to futex wait");
			exit(-1);
		}
	}
}

static inline void wake_node(volatile int *var)
{
	*var = S_READY;
	int ret = sys_futex((int *)var, FUTEX_WAKE_PRIVATE, 1, NULL, 0, 0);
	if (ret == -1) {
		perror("Unable to futex wake");
		exit(-1);
	}
}

/* Helper functions */
void *utablocking_alloc_cache_align(size_t n)
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

utablocking_mutex_t *utablocking_mutex_create(const pthread_mutexattr_t * attr)
{
	utablocking_mutex_t *impl = (utablocking_mutex_t *)
	    utablocking_alloc_cache_align(sizeof(utablocking_mutex_t));
	impl->tail = 0;
	return impl;
}

static int __utablocking_mutex_trylock(utablocking_mutex_t * impl,
				       utablocking_node_t * me)
{
	utablocking_node_t *expected;
	assert(me != NULL);
	me->next = NULL;
	expected = NULL;
	return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
					   __ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

static void waking_queue(utablocking_mutex_t * impl, utablocking_node_t * me)
{
	int i = 10;
	while (me && i != 0) {
		i--;
		wake_node(&me->status);
		me->status = S_ACTIVE;
		me = me->next;
	}
}

static void __utablocking_mutex_unlock(utablocking_mutex_t * impl,
				       utablocking_node_t * me)
{
	utablocking_node_t *succ, *next = me->next, *expected;
	uint64_t spin;
	utablocking_node_t *prevHead =
	    (utablocking_node_t *) (me->spin & 0xFFFFFFFFFFFF);
	utablocking_node_t *secHead, *secTail, *cur;
	int expected_int;

	int find = 0;
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
				wake_node(&prevHead->status);
				waking_queue(impl, prevHead->next);
				__atomic_store_n(&prevHead->spin,
						 0x1000000000000,
						 __ATOMIC_RELEASE);
				goto out;
			}
		}
		while (!(next = me->next))
			CPU_PAUSE();
	}
	succ = NULL;
	expected_int = S_ACTIVE;
	if (next->status == S_ACTIVE
	    && __atomic_compare_exchange_n(&next->status, &expected_int,
					   S_READY, 0, __ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED)) {
		find = 1;
		cur = next;
		goto find_out;
	}
	/* Next is not active, traverse the queue */
	secHead = next;
	secTail = next;
	cur = next->next;
	while (cur) {
		expected_int = S_ACTIVE;
		if (cur->status == S_ACTIVE
		    && __atomic_compare_exchange_n(&cur->status, &expected_int,
						   S_READY, 0, __ATOMIC_ACQ_REL,
						   __ATOMIC_RELAXED)) {
			if (prevHead) {
				prevHead->secTail->next = secHead;
			} else {
				prevHead = secHead;
			}
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
		spin = (uint64_t) prevHead | 0x1000000000000;	/* batch + 1 */
		/* Important: spin should not be 0 */
		/* Release barrier */
		__atomic_store_n(&cur->spin, spin, __ATOMIC_RELEASE);
		goto out;
	}

	/* Not find anything or batch */
	if (prevHead) {
		/* Secondary queue */
		prevHead->secTail->next = me->next;
		spin = 0x1000000000000;	/* batch = 1 */
		/* Release barrier */
		wake_node(&prevHead->status);
		if (prevHead->next)
			waking_queue(impl, prevHead->next);
		__atomic_store_n(&prevHead->spin, spin, __ATOMIC_RELEASE);
	} else {
		succ = me->next;
		spin = 0x1000000000000;	/* batch = 1 */
		/* Release barrier after */
		wake_node(&succ->status);
		if (succ->next)
			waking_queue(impl, succ->next);
		__atomic_store_n(&succ->spin, spin, __ATOMIC_RELEASE);
	}
 out:
	nested_level--;		/* Important! reduce nested level *after* release the lock */
	me->actcnt++;		/* Out of Critical Path */
}

#define DEFAULT_ACT_DURATION_TIME	1000000
#define MINIMAL_CHECK			1000
#define ALLOW_BATCH			40
#define DEFUALT_ACT_REDUCATION		(DEFAULT_ACT_DURATION_TIME/ALLOW_BATCH)

/* Using the unmodified MCS lock as the default underlying lock. */
static int __utablocking_lock_ux(utablocking_mutex_t * impl,
				 utablocking_node_t * me)
{
	utablocking_node_t *tail;
	uint64_t timestamp = rdtscp();
	uint64_t random = 0;	//xor_random() % DEFAULT_ACT_DURATION_TIME;
	uint64_t act_duration;
	int expected;

	me->next = NULL;
	me->spin = 0;
	me->status = S_ACTIVE;
	tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
	if (tail) {
		__atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
		act_duration =
		    DEFAULT_ACT_DURATION_TIME + random -
		    me->actcnt * DEFUALT_ACT_REDUCATION;
		act_duration =
		    act_duration < MINIMAL_CHECK ? MINIMAL_CHECK : act_duration;
		while (me->spin == 0) {
			CPU_PAUSE();
			if (me->status != S_READY
			    && rdtscp() - timestamp >
			    DEFAULT_ACT_DURATION_TIME + random) {
				expected = S_ACTIVE;
				if (__atomic_compare_exchange_n
				    (&me->status, &expected, S_PARKED, 0,
				     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
					/* Add to the secondary queue */
					me->actcnt = 0;
					park_node(&me->status, S_PARKED);
					timestamp = rdtscp();
				}
			}
		}
	}
	MEMORY_BARRIER();
	return 0;
}

/* Entry Point: length  */
static int __utablocking_mutex_lock(utablocking_mutex_t * impl,
				    utablocking_node_t * me)
{
	__utablocking_lock_ux(impl, me);
}

/* lock function orignal*/
int utablocking_mutex_lock(utablocking_mutex_t * impl, utablocking_node_t * me)
{
	return __utablocking_mutex_lock(impl, me);	/* Default loc 0 */
}

int utablocking_mutex_trylock(utablocking_mutex_t * impl,
			      utablocking_node_t * me)
{

	if (!__utablocking_mutex_trylock(impl, me)) {
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
void utablocking_mutex_unlock(utablocking_mutex_t * impl,
			      utablocking_node_t * me)
{
	__utablocking_mutex_unlock(impl, me);
}

int utablocking_mutex_destroy(utablocking_mutex_t * lock)
{
#if COND_VAR
	REAL(pthread_mutex_destroy)
	    (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

int utablocking_cond_init(utablocking_cond_t * cond,
			  const pthread_condattr_t * attr)
{
	return 0;
}

int utablocking_cond_timedwait(utablocking_cond_t * cond,
			       utablocking_mutex_t * lock,
			       utablocking_node_t * me,
			       const struct timespec *ts)
{
	return 0;
}

int utablocking_cond_wait(utablocking_cond_t * cond, utablocking_mutex_t * lock,
			  utablocking_node_t * me)
{
	return utablocking_cond_timedwait(cond, lock, me, 0);
}

int utablocking_cond_signal(utablocking_cond_t * cond)
{
	return 0;
}

void utablocking_thread_start(void)
{
}

void utablocking_thread_exit(void)
{
}

void utablocking_application_init(void)
{
}

void utablocking_application_exit(void)
{
	printf("Dcnt 0 %lu Dcnt 1 %lu\n", debug_cnt_0, debug_cnt_1);
}

int utablocking_cond_broadcast(utablocking_cond_t * cond)
{
	return 0;
}

int utablocking_cond_destroy(utablocking_cond_t * cond)
{
	return 0;
}

void utablocking_init_context(lock_mutex_t * impl,
			      lock_context_t * context, int UNUSED(number))
{
	context->actcnt = 0;
}

/* New interfaces in Libutablocking */
/* set a thread is uxthread or not */
void set_ux(int is_ux)
{
	uxthread = is_ux;
}
