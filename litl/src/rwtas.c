#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <rwtas.h>

#include "waiting_policy.h"
#include "interpose.h"
#include "utils.h"

extern __thread unsigned int cur_thread_id;

rwtas_mutex_t *rwtas_mutex_create(const pthread_mutexattr_t * attr)
{
	rwtas_mutex_t *impl =
	    (rwtas_mutex_t *) alloc_cache_align(sizeof(rwtas_mutex_t));
	impl->spin_lock = UNLOCKED;
#if COND_VAR
	REAL(pthread_mutex_init) (&impl->posix_lock, attr);
#endif

	return impl;
}

int rwtas_mutex_lock(rwtas_mutex_t * impl, rwtas_context_t * UNUSED(me))
{
	int expected = UNLOCKED;
	while (!__atomic_compare_exchange_n
	       (&impl->spin_lock, &expected, LOCKED, 0, __ATOMIC_ACQUIRE,
		__ATOMIC_RELAXED)) {
		expected = UNLOCKED;
		while (impl->spin_lock == LOCKED)
			CPU_PAUSE();
	}
#if COND_VAR
	int ret = REAL(pthread_mutex_lock) (&impl->posix_lock);

	assert(ret == 0);
#endif
	return 0;
}

int rwtas_mutex_trylock(rwtas_mutex_t * impl, rwtas_context_t * UNUSED(me))
{
	int expected = UNLOCKED;
	if (__atomic_compare_exchange_n
	    (&impl->spin_lock, &expected, LOCKED, 0, __ATOMIC_ACQUIRE,
	     __ATOMIC_RELAXED)) {
#if COND_VAR
		int ret = 0;
		while ((ret =
			REAL(pthread_mutex_trylock) (&impl->posix_lock)) ==
		       EBUSY)
			CPU_PAUSE();

		assert(ret == 0);
#endif
		return 0;
	}

	return EBUSY;
}

void __rwtas_mutex_unlock(rwtas_mutex_t * impl)
{
	__atomic_store_n(&impl->spin_lock, UNLOCKED, __ATOMIC_RELEASE);
}

void rwtas_mutex_unlock(rwtas_mutex_t * impl, rwtas_context_t * UNUSED(me))
{
#if COND_VAR
	int ret = REAL(pthread_mutex_unlock) (&impl->posix_lock);
	assert(ret == 0);
#endif
	__rwtas_mutex_unlock(impl);
}

int rwtas_mutex_destroy(rwtas_mutex_t * lock)
{
#if COND_VAR
	REAL(pthread_mutex_destroy) (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

int rwtas_cond_init(rwtas_cond_t * cond, const pthread_condattr_t * attr)
{
#if COND_VAR
	return REAL(pthread_cond_init) (cond, attr);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int rwtas_cond_timedwait(rwtas_cond_t * cond, rwtas_mutex_t * lock,
			 rwtas_context_t * me, const struct timespec *ts)
{
#if COND_VAR
	int res;

	__rwtas_mutex_unlock(lock);

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

	rwtas_mutex_lock(lock, me);

	return res;
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int rwtas_cond_wait(rwtas_cond_t * cond, rwtas_mutex_t * lock,
		    rwtas_context_t * me)
{
	return rwtas_cond_timedwait(cond, lock, me, 0);
}

int rwtas_cond_signal(rwtas_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_signal) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int rwtas_cond_broadcast(rwtas_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_broadcast) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

int rwtas_cond_destroy(rwtas_cond_t * cond)
{
#if COND_VAR
	return REAL(pthread_cond_destroy) (cond);
#else
	fprintf(stderr, "Error cond_var not supported.");
	assert(0);
#endif
}

// rwlock
rwtas_rwlock_t *rwtas_rwlock_create(const pthread_rwlockattr_t * attr)
{
	rwtas_rwlock_t *impl =
	    (rwtas_rwlock_t *) alloc_cache_align(sizeof(rwtas_rwlock_t));
	impl->lock_data = 0;
#if COND_VAR
	REAL(pthread_rwlock_init) (&impl->posix_lock, attr);
#endif
	MEMORY_BARRIER();
	return impl;
}

int rwtas_rwlock_rdlock(rwtas_rwlock_t * impl, rwtas_context_t * UNUSED(me))
{
	rw_data_t read_cnt;
	all_data_t expected;

	do {
		while ((read_cnt = impl->lock_data) > MAX_RW)
			CPU_PAUSE();
		expected = read_cnt;
	} while (!__atomic_compare_exchange_n
		 (&impl->lock_data, &expected, read_cnt + 1, 0,
		  __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
#if COND_VAR
	int ret = REAL(pthread_rwlock_rdlock) (&impl->posix_lock);
	assert(ret == 0);
#endif
	return 0;
}

int rwtas_rwlock_wrlock(rwtas_rwlock_t * impl, rwtas_context_t * UNUSED(me))
{

	all_data_t expected;

	while (!__atomic_compare_exchange_n
	       (&impl->lock_data, &expected, W_MASK, 0, __ATOMIC_ACQUIRE,
		__ATOMIC_RELAXED)) {
		while (impl->lock_data != 0)
			CPU_PAUSE();
		expected = 0;
	}
#if COND_VAR
	int ret = REAL(pthread_rwlock_wrlock) (&impl->posix_lock);

	assert(ret == 0);
#endif
	return 0;
}

int rwtas_rwlock_tryrdlock(rwtas_rwlock_t * impl, rwtas_context_t * UNUSED(me))
{
	rw_data_t read_cnt;
	all_data_t expected;

	if ((read_cnt = impl->lock_data) > MAX_RW)
		return EBUSY;
	expected = read_cnt;
	if (__atomic_compare_exchange_n
	    (&impl->lock_data, &expected, read_cnt + 1, 0, __ATOMIC_ACQUIRE,
	     __ATOMIC_RELAXED)) {
#if COND_VAR
		int ret = 0;
		while ((ret =
			REAL(pthread_rwlock_tryrdlock) (&impl->posix_lock)) ==
		       EBUSY)
			CPU_PAUSE();

		assert(ret == 0);
#endif
		return 0;
	}

	return EBUSY;
}

int rwtas_rwlock_trywrlock(rwtas_rwlock_t * impl, rwtas_context_t * UNUSED(me))
{
	all_data_t expected = 0;

	if (__atomic_compare_exchange_n
	    (&impl->lock_data, &expected, W_MASK, 0, __ATOMIC_ACQUIRE,
	     __ATOMIC_RELAXED)) {
#if COND_VAR
		int ret = 0;
		while ((ret =
			REAL(pthread_rwlock_trywrlock) (&impl->posix_lock)) ==
		       EBUSY)
			CPU_PAUSE();

		assert(ret == 0);
#endif
		return 0;
	}

	return EBUSY;
}

void __rwtas_rwlock_rdunlock(rwtas_rwlock_t * impl)
{
	__atomic_sub_fetch(&impl->lock_data, 1, __ATOMIC_RELEASE);
}

void __rwtas_rwlock_wrunlock(rwtas_rwlock_t * impl)
{
	__atomic_store_n(&impl->lock_data, 0, __ATOMIC_RELEASE);
}

int rwtas_rwlock_unlock(rwtas_rwlock_t * impl, rwtas_context_t * UNUSED(me))
{
#if COND_VAR
	int ret = REAL(pthread_rwlock_unlock) (&impl->posix_lock);
	assert(ret == 0);
#endif

	if (impl->lock_data > MAX_RW)	/* writer */
		__rwtas_rwlock_wrunlock(impl);
	else
		__rwtas_rwlock_rdunlock(impl);

	return 0;
}

int rwtas_rwlock_destroy(rwtas_rwlock_t * lock)
{
#if COND_VAR
	REAL(pthread_rwlock_destroy) (&lock->posix_lock);
#endif
	free(lock);
	lock = NULL;

	return 0;
}

void rwtas_thread_start(void)
{
}

void rwtas_thread_exit(void)
{
}

void rwtas_application_init(void)
{
}

void rwtas_application_exit(void)
{
}

void rwtas_init_context(lock_mutex_t * UNUSED(impl),
			lock_context_t * UNUSED(context), int UNUSED(number))
{
}
