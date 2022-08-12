#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <utascl.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <topology.h>
#include <papi.h>

#include "interpose.h"
#include "utils.h"

#include "waiting_policy.h"

/* Default Number */
#define DEFAULT_REORDER         100
#define MAX_REORDER             1000000000
#define REORDER_THRESHOLD       1000
int num_cnt;

extern __thread unsigned int cur_thread_id;
__thread unsigned int uxthread;
void *utascl_alloc_cache_align(size_t n)
{
	void *res = 0;
	if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res) {
		fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
			(unsigned long long)cache_align(n));
		exit(-1);
	}
	return res;
}

uint64_t get_current_tick(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000000000LL + now.tv_nsec;
}

utascl_mutex_t *utascl_mutex_create(const pthread_mutexattr_t * attr)
{
	utascl_mutex_t *impl = (utascl_mutex_t *)
	    utascl_alloc_cache_align(sizeof(utascl_mutex_t));
	impl->tail = 0;
	impl->max_hold_time = 0;
#if COND_VAR
	REAL(pthread_mutex_init) (&impl->posix_lock, /*&errattr */ attr);
#endif
	return impl;
}

static int __utascl_mutex_trylock(utascl_mutex_t * impl, utascl_node_t * me)
{
	utascl_node_t *expected;
	assert(me != NULL);
	me->next = NULL;
	expected = NULL;
	return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
					   __ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __utascl_lock_fifo(utascl_mutex_t * impl, utascl_node_t * me)
{
	utascl_node_t *tail;
	me->next = NULL;
	tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
	if (tail) {
		me->spin = 0;
		__atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
		while (me->spin == 0)
			CPU_PAUSE();
	}
	MEMORY_BARRIER();
	return 0;
}

static inline int __utascl_lock_reorder(utascl_mutex_t * impl,
					utascl_node_t * me)
{
	uint64_t reorder_window_ddl;
	uint64_t current_tick;
	uint32_t cnt = 0, next_check = 100;
	long long wait_time, local_hold_time = me->hold_time;

	while (impl->tail) {
		wait_time = impl->max_hold_time - local_hold_time;
		if (wait_time)
			goto out;
		reorder_window_ddl = PAPI_get_real_cyc() + wait_time;
		while ((current_tick =
			PAPI_get_real_cyc()) < reorder_window_ddl) {
			if (impl->tail == NULL)
				goto out;
		}
	}
 out:
	return __utascl_lock_fifo(impl, me);
}

static inline int __utascl_lock_eventually(utascl_mutex_t * impl,
					   utascl_node_t * me)
{
	uint64_t reorder_window_ddl;
	uint64_t current_tick;
	uint32_t cnt = 0, next_check = 100;

	/* Fast Path */
	if (impl->tail == NULL) {
		return __utascl_lock_fifo(impl, me);
	}
	/* Someone is holding the lock */
	reorder_window_ddl = PAPI_get_real_cyc() + MAX_REORDER;
	while ((current_tick = PAPI_get_real_cyc()) < reorder_window_ddl) {
		if (impl->tail == NULL)
			break;
	}
	return __utascl_lock_fifo(impl, me);
}

static int __utascl_mutex_lock(utascl_mutex_t * impl, utascl_node_t * me)
{
	long long local_hold_time = me->hold_time;

	if (uxthread) {
		if (local_hold_time < impl->max_hold_time) {
			return __utascl_lock_fifo(impl, me);
		} else {
			return __utascl_lock_reorder(impl, me);
		}
	} else
		return __utascl_lock_eventually(impl, me);
}

int utascl_mutex_lock(utascl_mutex_t * impl, utascl_node_t * me)
{
	int ret = __utascl_mutex_lock(impl, me);
	me->start_ts = PAPI_get_real_cyc();
	assert(ret == 0);
#if COND_VAR
	assert(REAL(pthread_mutex_lock) (&impl->posix_lock) == 0);
#endif
	return ret;
}

int utascl_mutex_trylock(utascl_mutex_t * impl, utascl_node_t * me)
{
	if (!__utascl_mutex_trylock(impl, me)) {
#if COND_VAR
		REAL(pthread_mutex_lock) (&impl->posix_lock);
#endif
		return 0;
	}
	return -EBUSY;
}

static void __utascl_mutex_unlock(utascl_mutex_t * impl, utascl_node_t * me)
{
	utascl_node_t *expected;
	long long duration = PAPI_get_real_cyc() - me->start_ts;
	me->hold_time += duration;
	if (me->hold_time > impl->max_hold_time)
		impl->max_hold_time = me->hold_time;

	if (!me->next) {
		expected = me;
		if (__atomic_compare_exchange_n(&impl->tail, &expected, 0, 0,
						__ATOMIC_RELEASE,
						__ATOMIC_RELAXED)) {
			goto out;
		}
		while (!me->next)
			CPU_PAUSE();
	}
	MEMORY_BARRIER();
	me->next->spin = 1;
 out:
	return;
}

void utascl_mutex_unlock(utascl_mutex_t * impl, utascl_node_t * me)
{
#if COND_VAR
	assert(REAL(pthread_mutex_unlock) (&impl->posix_lock) == 0);
#endif
	__utascl_mutex_unlock(impl, me);
}

int utascl_mutex_destroy(utascl_mutex_t * lock)
{
#if COND_VAR
	REAL(pthread_mutex_destroy) (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

int utascl_cond_init(utascl_cond_t * cond, const pthread_condattr_t * attr)
{
#if COND_VAR
	return REAL(pthread_cond_init) (cond, attr);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int utascl_cond_timedwait(utascl_cond_t * cond, utascl_mutex_t * lock,
			  utascl_node_t * me, const struct timespec *ts)
{
#if COND_VAR
	int res;

	__utascl_mutex_unlock(lock, me);

	if (ts)
		res =
		    REAL(pthread_cond_timedwait) (cond, &lock->posix_lock, ts);
	else
		res = REAL(pthread_cond_wait) (cond, &lock->posix_lock);

	if (res != 0 && res != ETIMEDOUT) {
		fprintf(stderr, "Error on cond_{timed,}wait %d\n", res);
		assert(0);
	}

	int ret = 0;
	if ((ret = REAL(pthread_mutex_unlock) (&lock->posix_lock)) != 0) {
		fprintf(stderr, "Error on mutex_unlock %d\n", ret == EPERM);
		assert(0);
	}

	utascl_mutex_lock(lock, me);

	return res;
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int utascl_cond_wait(utascl_cond_t * cond, utascl_mutex_t * lock,
		     utascl_node_t * me)
{
	return utascl_cond_timedwait(cond, lock, me, 0);
}

int utascl_cond_signal(utascl_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_signal) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int utascl_cond_broadcast(utascl_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_broadcast) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int utascl_cond_destroy(utascl_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_destroy) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

void utascl_thread_start(void)
{
}

void utascl_thread_exit(void)
{
}

void utascl_application_init(void)
{
}

void utascl_application_exit(void)
{
}

void utascl_init_context(lock_mutex_t * UNUSED(impl),
			 lock_context_t * UNUSED(context), int UNUSED(number))
{
}

/* New interfaces in Libutascl */

void set_ux(int is_ux)
{
	uxthread = is_ux;
}
