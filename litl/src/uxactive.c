#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <uxactive.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <topology.h>

#include "libuxactive.h"
#include "interpose.h"
#include "utils.h"

#include "waiting_policy.h"

/* Default Number */
#define DEFAULT_REORDER         100
#define MAX_REORDER             1000000000
#define REORDER_THRESHOLD       1000
int num_cnt;
/* Epoch Information */
#define MAX_EPOCH	256
typedef struct {
	uint64_t reorder_window;
	uint64_t adjust_unit;
	struct timespec start_ts;
} epoch_t;

__thread epoch_t epoch[MAX_EPOCH] = { 0 };

__thread int cur_epoch_id = -1;
extern __thread unsigned int cur_thread_id;
__thread unsigned int uxthread;
void *uxactive_alloc_cache_align(size_t n)
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

uxactive_mutex_t *uxactive_mutex_create(const pthread_mutexattr_t * attr)
{
	uxactive_mutex_t *impl = (uxactive_mutex_t *)
	    uxactive_alloc_cache_align(sizeof(uxactive_mutex_t));
	impl->tail = 0;
#if COND_VAR
	REAL(pthread_mutex_init) (&impl->posix_lock, /*&errattr */ attr);
#endif
	return impl;
}

static int __uxactive_mutex_trylock(uxactive_mutex_t * impl,
				    uxactive_node_t * me)
{
	uxactive_node_t *expected;
	assert(me != NULL);
	me->next = NULL;
	expected = NULL;
	return __atomic_compare_exchange_n(&impl->tail, &expected, me, 0,
					   __ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED) ? 0 : -EBUSY;
}

/* Using the unmodified MCS lock as the default underlying lock. */
static int __uxactive_lock_fifo(uxactive_mutex_t * impl, uxactive_node_t * me)
{
	uxactive_node_t *tail;
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

static inline int __uxactive_lock_reorder(uxactive_mutex_t * impl,
					  uxactive_node_t * me,
					  uint64_t reorder_window)
{
	uint64_t reorder_window_ddl;
	uint64_t current_ns;
	uint32_t cnt = 0, next_check = 100;
	/* Fast Path */
	if (reorder_window < REORDER_THRESHOLD || impl->tail == NULL) {
		return __uxactive_lock_fifo(impl, me);
	}

	/* Someone is holding the lock */
	reorder_window_ddl = get_current_ns() + reorder_window;
	while ((current_ns = get_current_ns()) < reorder_window_ddl) {
		if (impl->tail == NULL) break;
	}
	return __uxactive_lock_fifo(impl, me);
}

static inline int __uxactive_lock_eventually(uxactive_mutex_t * impl,
					     uxactive_node_t * me)
{
	return __uxactive_lock_reorder(impl, me, MAX_REORDER);
}

static int __uxactive_mutex_lock(uxactive_mutex_t * impl, uxactive_node_t * me)
{
	if (uxthread)
		return __uxactive_lock_fifo(impl, me);
	else
		return __uxactive_lock_eventually(impl, me);
}

int uxactive_mutex_lock(uxactive_mutex_t * impl, uxactive_node_t * me)
{
	int ret = __uxactive_mutex_lock(impl, me);
	assert(ret == 0);
#if COND_VAR
	assert(REAL(pthread_mutex_lock) (&impl->posix_lock) == 0);
#endif
	return ret;
}

int uxactive_mutex_trylock(uxactive_mutex_t * impl, uxactive_node_t * me)
{
	if (!__uxactive_mutex_trylock(impl, me)) {
#if COND_VAR
		REAL(pthread_mutex_lock) (&impl->posix_lock);
#endif
		return 0;
	}
	return -EBUSY;
}

static void __uxactive_mutex_unlock(uxactive_mutex_t * impl,
				    uxactive_node_t * me)
{
	uxactive_node_t *expected;
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

void uxactive_mutex_unlock(uxactive_mutex_t * impl, uxactive_node_t * me)
{
#if COND_VAR
	assert(REAL(pthread_mutex_unlock) (&impl->posix_lock) == 0);
#endif
	__uxactive_mutex_unlock(impl, me);
}

int uxactive_mutex_destroy(uxactive_mutex_t * lock)
{
#if COND_VAR
	REAL(pthread_mutex_destroy) (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

int uxactive_cond_init(uxactive_cond_t * cond, const pthread_condattr_t * attr)
{
#if COND_VAR
	return REAL(pthread_cond_init) (cond, attr);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int uxactive_cond_timedwait(uxactive_cond_t * cond, uxactive_mutex_t * lock,
			    uxactive_node_t * me, const struct timespec *ts)
{
#if COND_VAR
	int res;

	__uxactive_mutex_unlock(lock, me);

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

	uxactive_mutex_lock(lock, me);

	return res;
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int uxactive_cond_wait(uxactive_cond_t * cond, uxactive_mutex_t * lock,
		       uxactive_node_t * me)
{
	return uxactive_cond_timedwait(cond, lock, me, 0);
}

int uxactive_cond_signal(uxactive_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_signal) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int uxactive_cond_broadcast(uxactive_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_broadcast) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int uxactive_cond_destroy(uxactive_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_destroy) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

void uxactive_thread_start(void)
{
}

void uxactive_thread_exit(void)
{
}

void uxactive_application_init(void)
{
}

void uxactive_application_exit(void)
{
}

void uxactive_init_context(lock_mutex_t * UNUSED(impl),
			   lock_context_t * UNUSED(context), int UNUSED(number))
{
}

/* New interfaces in Libuxactive */

void set_ux(int is_ux)
{
	uxthread = is_ux;
}
