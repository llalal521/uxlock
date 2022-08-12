#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <utaspc.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <papi.h>

#include "utaspc.h"
#include "interpose.h"
#include "utils.h"

/* Default Number */
#define NO_UX_MAX_WAIT_TIME     10000000000
#define SHORT_BATCH_THRESHOLD   1000	/* Should less than 2^16 65536 */
#define ADJUST_THRESHOLD	1
#define ADJUST_FREQ		100	/* Should less than SHORT_BATCH_THRESHOLD */
#define DEFAULT_SHORT_THRESHOLD	500
#define MAX_CS_LEN		10000000

#define DEFAULT_REORDER         100
#define MAX_REORDER             1000000000
#define DEFAULT_ADJUST_UNIT	100
#define MIN_ADJUST_UNIT		10
#define REORDER_THRESHOLD       1000
#define EPOCH_REQ_THRESHOLD	100

/* Epoch Information */
#define MAX_EPOCH	256
typedef struct {
	uint64_t reorder_window;
	uint64_t adjust_unit;
#ifdef TIME_CLOCK
	struct timespec start_ts;
#else
	long long start_ts;
#endif
} epoch_t;

__thread epoch_t epoch[MAX_EPOCH] = { 0 };

__thread int cur_epoch_id = -1;

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
void *utaspc_alloc_cache_align(size_t n)
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

utaspc_mutex_t *utaspc_mutex_create(const pthread_mutexattr_t * attr)
{
	utaspc_mutex_t *impl =
	    (utaspc_mutex_t *) utaspc_alloc_cache_align(sizeof(utaspc_mutex_t));
	impl->tail = 0;
	impl->threshold = DEFAULT_SHORT_THRESHOLD;
	return impl;
}

static int __utaspc_mutex_trylock(utaspc_mutex_t * impl, utaspc_node_t * me)
{
	utaspc_node_t *expected;
	assert(me != NULL);
	me->next = NULL;
	expected = NULL;
	return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
					   __ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

static void __utaspc_mutex_unlock(utaspc_mutex_t * impl, utaspc_node_t * me)
{
	utaspc_node_t *next = me->next, *expected;

	if (!next) {
		expected = me;
		if (__atomic_compare_exchange_n
		    (&impl->tail, &expected, 0, 0, __ATOMIC_RELEASE,
		     __ATOMIC_RELAXED)) {
			goto out;
		}
		while (!(next = me->next))
			CPU_PAUSE();
	}
	/* Release barrier after */
	__atomic_store_n(&next->spin, 1, __ATOMIC_RELEASE);
 out:
	nested_level--;		/* Important! reduce nested level *after* release the lock */
}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __utaspc_lock_ux(utaspc_mutex_t * impl, utaspc_node_t * me)
{
	utaspc_node_t *tail;
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
static inline int __utaspc_lock_reorder(utaspc_mutex_t * impl,
					utaspc_node_t * me, uint64_t window)
{
	uint64_t reorder_window_ddl;
	uint32_t cnt = 0;
	if (impl->tail == NULL)
		goto out;
	/* Someone is holding the lock */
	reorder_window_ddl = get_current_ns() + window;
	while (get_current_ns() < reorder_window_ddl) {
		/* Spin-check if the queue is empty */
		if (impl->tail == NULL) {
			goto out;
		}
	}
 out:
	return __utaspc_lock_ux(impl, me);
}

/* not-ux-thread reorder if queue not empty */
static inline int __utaspc_lock_nonux(utaspc_mutex_t * impl, utaspc_node_t * me)
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
	return __utaspc_lock_ux(impl, me);
}

/* Entry Point: length  */
static int __utaspc_mutex_lock(utaspc_mutex_t * impl, utaspc_node_t * me)
{
	int ret;
	if (nested_level > 1)
		return __utaspc_lock_ux(impl, me);
	if (uxthread) {
		if (me->cri_len < impl->threshold) {
			// printf("%d lock short!\n", sched_getcpu());
			return __utaspc_lock_ux(impl, me);
		} else {
			// printf("%d lock long window %ld !\n", sched_getcpu(), epoch[cur_epoch_id].reorder_window);
			return __utaspc_lock_reorder(impl, me,
						     epoch
						     [cur_epoch_id].reorder_window);
		}
	} else {
		return __utaspc_lock_nonux(impl, me);
	}
}

/* lock function with perdict critical (Do not support litl, use llvm instead) */
int utaspc_mutex_lock_cri(utaspc_mutex_t * impl, utaspc_node_t * me, int loc)
{
	me->cri_len = critical_len[loc];
	nested_level++;		/* Per-thread nest level cnter, add before hold the lock */
	int ret = __utaspc_mutex_lock(impl, me);
	/* Critical Section Start */
	tt_start[loc] = PAPI_get_real_cyc();
	if (cur_loc >= 0)
		push_loc(cur_loc);
	cur_loc = loc;		/* No need to read stack in critical path */
	return ret;
}

/* lock function orignal*/
int utaspc_mutex_lock(utaspc_mutex_t * impl, utaspc_node_t * me)
{
	return utaspc_mutex_lock_cri(impl, me, 0);	/* Default loc 0 */
}

int utaspc_mutex_trylock(utaspc_mutex_t * impl, utaspc_node_t * me)
{

	if (!__utaspc_mutex_trylock(impl, me)) {
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
void utaspc_mutex_unlock(utaspc_mutex_t * impl, utaspc_node_t * me)
{
	uint64_t duration, end_ts;
	/* Record CS len */
	end_ts = PAPI_get_real_cyc();
	__utaspc_mutex_unlock(impl, me);
	duration = end_ts - tt_start[cur_loc];
	/* update critical_len */
	if (critical_len[cur_loc] == 0)
		critical_len[cur_loc] = duration;
	else if (duration < MAX_CS_LEN)	/* Not too long to record */
		critical_len[cur_loc] =
		    ((critical_len[cur_loc] * 7) + duration) >> 3;
	cur_loc = pop_loc();
}

int utaspc_mutex_destroy(utaspc_mutex_t * lock)
{
#if COND_VAR
	REAL(pthread_mutex_destroy)
	    (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

int utaspc_cond_init(utaspc_cond_t * cond, const pthread_condattr_t * attr)
{
	return 0;
}

int utaspc_cond_timedwait(utaspc_cond_t * cond, utaspc_mutex_t * lock,
			  utaspc_node_t * me, const struct timespec *ts)
{
	return 0;
}

int utaspc_cond_wait(utaspc_cond_t * cond, utaspc_mutex_t * lock,
		     utaspc_node_t * me)
{
	return utaspc_cond_timedwait(cond, lock, me, 0);
}

int utaspc_cond_signal(utaspc_cond_t * cond)
{
	return 0;
}

void utaspc_thread_start(void)
{
	for (int i = 0; i < MAX_EPOCH; i++) {
		epoch[i].reorder_window = DEFAULT_REORDER;
		epoch[i].adjust_unit = DEFAULT_ADJUST_UNIT;
	}
	cur_epoch_id = -1;
}

void utaspc_thread_exit(void)
{
}

void utaspc_application_init(void)
{
}

void utaspc_application_exit(void)
{
}

int utaspc_cond_broadcast(utaspc_cond_t * cond)
{
	return 0;
}

int utaspc_cond_destroy(utaspc_cond_t * cond)
{
	return 0;
}

void utaspc_init_context(lock_mutex_t * UNUSED(impl),
			 lock_context_t * UNUSED(context), int UNUSED(number))
{
}

/* New interfaces in Libutaspc */
/* set a thread is uxthread or not */
void set_ux(int is_ux)
{
	uxthread = is_ux;
}

/* A stack to implement nested epoch */
#define MAX_DEPTH 30
__thread int epoch_stack[MAX_DEPTH];
__thread int epoch_stack_pos = -1;

/* Per-thread private stack */
int push_epoch(int epoch_id)
{
	epoch_stack_pos++;
	if (epoch_stack_pos == MAX_EPOCH)
		return -ENOSPC;
	epoch_stack[epoch_stack_pos] = epoch_id;
	return 0;
}

/* Per-thread private stack */
int pop_epoch(void)
{
	if (epoch_stack_pos < 0)
		return -EINVAL;
	return epoch_stack[epoch_stack_pos--];
}

int is_stack_empty(void)
{
	return epoch_stack_pos < 0;
}

/* New interfaces in LibASL */

/* Epoch-based interface */
int epoch_start(int epoch_id)
{
	if (epoch_id < 0 || epoch_id > MAX_EPOCH || cur_epoch_id < -1)
		return -EINVAL;
	if (cur_epoch_id != -1 && push_epoch(cur_epoch_id) < 0)
		return -ENOSPC;
	/* Set cur_epoch_id */
	cur_epoch_id = epoch_id;
	/* Get the epoch start time */
#ifdef TIME_CLOCK
	clock_gettime(CLOCK_MONOTONIC, &epoch[cur_epoch_id].start_ts);
#endif
	epoch[cur_epoch_id].start_ts = PAPI_get_real_cyc();
	return 0;
}

int epoch_end(int epoch_id, uint64_t required_latency)
{
	struct timespec epoch_end_ts;
	uint64_t duration = 0;
	uint64_t reorder_window = epoch[cur_epoch_id].reorder_window;

	/* Fast out */
	if (required_latency < EPOCH_REQ_THRESHOLD) {
		epoch[cur_epoch_id].reorder_window = 0;
		goto out;
	}
	if (epoch_id < 0 || epoch_id > MAX_EPOCH)
		return -EINVAL;
	if (epoch_id != cur_epoch_id)
		return -EINVAL;

	/* Get the epoch end time */
#ifdef TIME_CLOCK
	clock_gettime(CLOCK_MONOTONIC, &epoch_end_ts);
	duration =
	    (epoch_end_ts.tv_sec -
	     epoch[cur_epoch_id].start_ts.tv_sec) * 1000000000LL +
	    epoch_end_ts.tv_nsec - epoch[cur_epoch_id].start_ts.tv_nsec;
#else
	duration = PAPI_get_real_cyc() - epoch[cur_epoch_id].start_ts;
#endif
	/* Adjust the reorder window */
	if (duration > required_latency) {
		reorder_window >>= 1;
		epoch[cur_epoch_id].adjust_unit = reorder_window / 99;
		if (epoch[cur_epoch_id].adjust_unit < MIN_ADJUST_UNIT)
			epoch[cur_epoch_id].adjust_unit = MIN_ADJUST_UNIT;
	} else {
		reorder_window += epoch[cur_epoch_id].adjust_unit;
	}
	epoch[cur_epoch_id].reorder_window = reorder_window;
 out:
	/* Support nested epoches */
	if (is_stack_empty())
		cur_epoch_id = -1;
	else
		cur_epoch_id = pop_epoch();
	return 0;
}
