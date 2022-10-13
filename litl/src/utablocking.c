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

#define NODE_ACTIVE 1
#define NODE_SLEEP 0

#define S_ACTIVE 2
#define S_PARKED 0
#define S_READY 1
#define S_REQUE 3 

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
		//// // // // // // // // // printf("Loc Stack FULL!\n");
		return -1;
	}
	loc_stack[stack_pos] = loc;
	return 0;
}

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
	__asm __volatile("rdtscp; mov %%eax, %0; mov %%edx, %1; cpuid"
			 : "=r" (a), "=r" (d)
			 : : "%rax", "%rbx", "%rcx", "%rdx");
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}

// void printList(utablocking_mutex_t * impl, utablocking_node_t * me, int i) 
// {
// 	utablocking_node_t *cur = me;
// 	utablocking_node_t *prevHead = (utablocking_node_t *) (me->spin & 0xFFFFFFFFFFFF);
// 	// // // // // // // // // printf("loc %d", i);
// 	while(cur) {
// 		// // // // // // // // // printf("->[%d %d %d %d %lu]", cur->tid, cur->status, i++, cur->cri_len, cur->spin);
// 		cur = cur->next;
// 	}

// 	// if(!prevHead)
// 	// 	goto printOut;
// 	// // // // // // // // // // printf("->second ");
// 	// while(prevHead) {
// 	// 	// // // // // // // // // printf("->[%d %d %d]",prevHead->tid, prevHead->status, prevHead->cri_len);
// 	// 	prevHead = prevHead->next;
// 	// }
// printOut:
// 	// // // // // // printf("\n");
// }
/* Per-thread private stack */
int pop_loc(void)
{
	if (stack_pos < 0)
		return -1;	/* Return -1, give it to cur_loc. */
	return loc_stack[stack_pos--];
}

static inline void park_node(volatile int *var, int target) {
    // First spin with a given threshold.
    int ret = 0;
    while ((ret = sys_futex((int *)var, FUTEX_WAIT_PRIVATE, target, NULL, 0,
                            0)) != 0) {
        if (ret == -1 && errno != EINTR) {
            /**
             * futex returns EAGAIN if *var is not 0 anymore.
             * This can happen when the value of *var is changed by another
             *thread after the spinning loop.
             * Note: FUTEX_WAIT_PRIVATE acts like an atomic operation.
             **/
            if (errno == EAGAIN) {
                // // // // printf("ddd\n");
                DEBUG("[-1] Race\n");
                break;
            }
            perror("Unable to futex wait");
            exit(-1);
        }
    }
    // // // // printf("ddd1\n");
    /**
     * *var is not always 1 immediately when the thread wakes up
     * (but eventually it is).
     * Maybe related to memory reordering?
     **/
    while (*var != UNLOCKED)
        CPU_PAUSE();
}

static inline void wake_node(volatile int *var) {
	*var = S_READY;
    int ret = sys_futex((int *)var, FUTEX_WAKE_PRIVATE, 1, NULL, 0, 0);
	// // // // printf("wake numver %d\n", ret);
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
	utablocking_mutex_t *impl =
	    (utablocking_mutex_t *) utablocking_alloc_cache_align(sizeof(utablocking_mutex_t));
	impl->tail = 0;
	impl->threshold = DEFAULT_SHORT_THRESHOLD;
	return impl;
}

static int __utablocking_mutex_trylock(utablocking_mutex_t * impl, utablocking_node_t * me)
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
	// me->status = S_READY;
	// wake_node(&me->wait);
	// me = me->next;
	int i = 9;
	while(me && i != 0) {
		i--;
		// __atomic_store_n(&me->status, NODE_ACTIVE, __ATOMIC_RELEASE);
		// // // // // // // printf("set tid %d wake status %d cur %d\n", me->tid, me->wait, cur_thread_id);
		wake_node(&me->status);
		me->status = S_ACTIVE;
		// // // // // // // printf("set tid %d wake status %d cur %d\n", me->tid, me->wait, cur_thread_id);
		me = me->next;
	}
}

void printPrevHead(utablocking_node_t *me, utablocking_node_t * prevHead) {
	utablocking_node_t *tmp;
	// // // printf("me [%d %d]", me->tid, me->status);
	tmp = me->next;
	while(tmp) {
		// // // printf("->[%d %d]", tmp->tid, tmp->status);
		tmp = tmp->next;
	}
	// // // printf("\n");
	if(prevHead) {
		// // // printf("prevHead [%d %d]", prevHead->tid, prevHead->status);
	tmp = prevHead->next;
	int k = 20;
	while(tmp && k > 0) {
		// // // printf("->[%d %d]", tmp->tid, tmp->status);
		k--;
		tmp = tmp->next;
	}
	// // // printf("\n");
	}
	
}
static void __utablocking_mutex_unlock(utablocking_mutex_t * impl, utablocking_node_t * me)
{
	utablocking_node_t *succ, *next = me->next, *expected, *tmp;
	uint64_t spin;
	int k = 5, flag = 0;
	uint64_t batch = (me->spin >> 48) & 0xFFFF;
	utablocking_node_t *prevHead = (utablocking_node_t *) (me->spin & 0xFFFFFFFFFFFF);
	utablocking_node_t *secHead, *secTail, *cur;
	int32_t threshold = impl->threshold;
	int sta_exp;
	
	int find = 0;
	// // printf("tid %d unlock 1\n", me->tid);
	if (!next) {
		// // printf("tid %d unlock p\n", me->tid);
		if (!prevHead) {
			expected = me;
			if (__atomic_compare_exchange_n
			    (&impl->tail, &expected, 0, 0, __ATOMIC_RELEASE,
			     __ATOMIC_RELAXED)) {
					// // printf("tid %d unlock 2\n", me->tid);
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
				
				// printList(impl, prevHead, 0);
				// // printf("prevHead->tid %d should lock status tt %d\n", prevHead->tid, prevHead->status);
				
				
				goto out;
			}
		}
		// // printf("tid %d unlock in while\n", me->tid);
		while (!(next = me->next))
			CPU_PAUSE();
	}
	// // printf("tid %d unlock 2\n", me->tid);
	// if(xor_random() % 5000 == 0) {
	// 	flag = 1;
	// }
	flag = 1;
	succ = NULL;
	if (batch < SHORT_BATCH_THRESHOLD) {
		/* Find next short CS */
		sta_exp = 2;
		if(__atomic_compare_exchange_n(&next->status, &sta_exp, 1, 0,  
						__ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED)) {
			// // // // // // // // // // printf("cur_thread_id %d is short %d\n", cur_thread_id, next->cri_len);
			find = 1;
			cur = next;
			goto find_out;
		}
		secHead = next;
		secTail = next;
		cur = next->next;
		while (cur) {
			sta_exp = 2;
			if (__atomic_compare_exchange_n(&cur->status, &sta_exp, 1, 0,  
						__ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED)) {
				if (prevHead) {
					prevHead->secTail->next = secHead;
				}
				else {
					prevHead = secHead;
					
				}
				secTail->next = NULL;
				prevHead->secTail = secTail;
				// waiting_queue(impl, secHead);
				find = 1;
				break;
			}
			secTail = cur;
			cur = cur->next;
		}
 find_out:
		if (find) {
			// printf("tid %d unlock 3\n", me->tid);
			if(prevHead && flag) {
				if(cur->next && cur->next->next) {
					// printf("tid %d in sec tail\n", cur->next->tid);
					cur->next->status = S_PARKED;
					tmp = cur->next;
					cur->next = cur->next->next;
					tmp->next = NULL;
					prevHead->secTail->next = tmp;
					prevHead->secTail = tmp;
					// // printf("here1\n");
				}
				if(prevHead->next) {
					// // // printf("new prevHead %d\n", prevHead->next->tid);
					prevHead->next->secTail = prevHead->secTail;
				}
					
				tmp = prevHead->next;
				// // // // printf("wake node %d %d\n", prevHead->tid, prevHead->status);
				// // // printf("wake %d %llx\n", prevHead->tid, &prevHead->status);
				wake_node(&prevHead->status);
				
				
				prevHead->wait = S_REQUE;
				// printf("prevHead %d reque\n", prevHead->tid);
				// MEMORY_BARRIER();
				spin = (uint64_t) tmp | ((batch + 1) << 48);	/* batch + 1 */
				/* Important: spin should not be 0 */
				/* Release barrier */
				__atomic_store_n(&cur->spin, spin, __ATOMIC_RELEASE);
				// prevHead->status = S_REQUE;
				// // printf("cur->tid %d should lock status %d\n", cur->tid, cur->status);
				goto out;
				
			}
			spin = (uint64_t) prevHead | ((batch + 1) << 48);	/* batch + 1 */
			/* Important: spin should not be 0 */
			/* Release barrier */
			// //// // // // // // // // // printf("cur_thread_id %d unlock 6\n", cur_thread_id);
			__atomic_store_n(&cur->spin, spin, __ATOMIC_RELEASE);
			// // printf("cur->tid %d should lock status %d\n", cur->tid, cur->status);
				// printPrevHead(cur, prevHead);
			
			// printList(impl, cur, 2);
			goto out;
		} 
	}
	// // printf("tid %d unlock 5\n", me->tid);
	/* Not find anything or batch */
	if (prevHead) {;
		prevHead->secTail->next = me->next;
		spin = 0x1000000000000;	/* batch = 1 */
		// // printList(impl, prevHead);
		/* Release barrier */
		wake_node(&prevHead->status);
		if(prevHead->next)
			waking_queue(impl, prevHead->next);
		// prevHead->status = S_READY;
		__atomic_store_n(&prevHead->spin, spin, __ATOMIC_RELEASE);
		// // printf("prevHead->tid %d should lock status pp %d\n", prevHead->tid, prevHead->status);
		// // // // // // // // // // printf("here wake\n");	
		// printList(impl, prevHead, 4);
		
		
		// //// // // // // // // // // printf("waiting wake\n");
		
	} else {
		// //// // // // // // // // // printf("succ %p %d %d\n", succ, impl->batch, impl->threshold);
		succ = me->next;
		spin = 0x1000000000000;	/* batch = 1 */
		/* Release barrier after */
		wake_node(&succ->status);
		// // // // printf("wake %d %d %d\n", succ->tid, succ->status, succ->wait);
		if(succ->next)
			waking_queue(impl, succ->next);
		// succ->status = S_READY;
		__atomic_store_n(&succ->spin, spin, __ATOMIC_RELEASE);
		// // printf("succ->tid %d should lock %d\n", succ->tid, succ->status);
		// // printList(impl, succ, 5);
	}
 out:
 	// printList(impl, me, 5);
	// // printf("tid %d unlock out\n", me->tid);
	nested_level--;		/* Important! reduce nested level *after* release the lock */
}


/* Using the unmodified MCS lock as the default underlying lock. */
static int __utablocking_lock_ux(utablocking_mutex_t * impl, utablocking_node_t * me)
{
	utablocking_node_t *tail;
	uint64_t timestamp = rdtscp();
	int random = xor_random() % 10000;
	int expected;
	me->tid = cur_thread_id;
reque:
	me->next = NULL;
	me->spin = 0;
	
	me->wait = 0;
	me->status = S_ACTIVE;

	// // // // printf("tid %d go lock\n", gettid());
	tail = __atomic_exchange_n(&impl->tail, me, __ATOMIC_RELEASE);
	if (tail) {
		__atomic_store_n(&tail->next, me, __ATOMIC_RELEASE);
		while (me->spin == 0) {
			CPU_PAUSE();
			// // // // // // printf("tid %d is here %d\n", me->tid, me->wait);
			if(rdtscp() - timestamp > 1000000 + random) {
				// me->wait = 0;
				// // // // // // printf("tid %d is sleep status %d\n", me->tid, me->wait);
				expected = S_ACTIVE;
				if(__atomic_compare_exchange_n(&me->status, &expected, 0, 0,  
						__ATOMIC_ACQ_REL,
					   __ATOMIC_ACQ_REL)) {
						// me->wait = 0;
						 // printf("tid %d is sleep wait status %d address %llx\n", me->tid, me->status, &me->status);
						park_node(&me->status, 0);
						 // printf("tid %d is waked wait status %d address %llx\n", me->tid, me->status, &me->status);
						
						
						timestamp = rdtscp();
					}
				
				// // // // // // printf("tid %d is waked status %d\n", me->tid, me->wait);
			}
			// if(me->status == S_PARKED) {
			// 				park_node(&me->status, 0);
			// 			}	

				if(me->wait == S_REQUE) {
							// // // printf("go reque %d\n", me->tid);
							goto reque;
						}	
		}
			
	} else {
		/* set batch to 0 */
		me->spin = 0;
	}
	MEMORY_BARRIER();
	// printf("tid %d lock succ %d\n", me->tid, me->status);
	// // // // // // // // // // printf("cur_thread_id %d lock succ\n", cur_thread_id);
	return 0;
}

/* not-ux-thread reorder if queue not empty */
static inline int __utablocking_lock_nonux(utablocking_mutex_t * impl, utablocking_node_t * me)
{
	uint64_t reorder_window_ddl;
	uint32_t cnt = 0;
	uint64_t current_ns;
	uint64_t sleep_time = 10000;
	if (utablocking_mutex_trylock(impl, me) == 0)
            return 0;
	/* Someone is holding the lock */
	reorder_window_ddl = get_current_ns() + NO_UX_MAX_WAIT_TIME;
	while (current_ns = get_current_ns() < reorder_window_ddl) {
        sleep_time = sleep_time < reorder_window_ddl - current_ns ?
            sleep_time : reorder_window_ddl - current_ns;
        nanosleep((const struct timespec[]){{0, sleep_time}}, NULL);
        if (utablocking_mutex_trylock(impl, me) == 0)
            return 0;
        sleep_time = sleep_time << 5;
	}
 out:
	return __utablocking_lock_ux(impl, me);
}

/* Entry Point: length  */
static int __utablocking_mutex_lock(utablocking_mutex_t * impl, utablocking_node_t * me)
{
	__utablocking_lock_ux(impl, me);
	// me->status = NODE_ACTIVE;
}

/* lock function with perdict critical (Do not support litl, use llvm instead) */
int utablocking_mutex_lock_cri(utablocking_mutex_t * impl, utablocking_node_t * me, int loc)
{
	nested_level++;		/* Per-thread nest level cnter, add before hold the lock */

	int ret = __utablocking_mutex_lock(impl, me);
	/* Critical Section Start */
	// tt_start[loc] = PAPI_get_real_cyc();
	// if (cur_loc >= 0)
	// 	push_loc(cur_loc);
	// cur_loc = loc;		/* No need to read stack in critical path */
	return ret;
}

/* lock function orignal*/
int utablocking_mutex_lock(utablocking_mutex_t * impl, utablocking_node_t * me)
{
	return __utablocking_mutex_lock(impl, me);	/* Default loc 0 */
}

int utablocking_mutex_trylock(utablocking_mutex_t * impl, utablocking_node_t * me)
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
void utablocking_mutex_unlock(utablocking_mutex_t * impl, utablocking_node_t * me)
{
	// uint64_t duration, end_ts;
	/* Record CS len */
	// end_ts = PAPI_get_real_cyc();
	__utablocking_mutex_unlock(impl, me);
	// duration = end_ts - tt_start[cur_loc];
	/* update critical_len */
	// cur_loc = pop_loc();
	// // // // // // // // // // printf("me->tid %d unlock\n", me->tid);
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

int utablocking_cond_init(utablocking_cond_t * cond, const pthread_condattr_t * attr)
{
	return 0;
}

int utablocking_cond_timedwait(utablocking_cond_t * cond, utablocking_mutex_t * lock,
		       utablocking_node_t * me, const struct timespec *ts)
{
	return 0;
}

int utablocking_cond_wait(utablocking_cond_t * cond, utablocking_mutex_t * lock, utablocking_node_t * me)
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
}

int utablocking_cond_broadcast(utablocking_cond_t * cond)
{
	return 0;
}

int utablocking_cond_destroy(utablocking_cond_t * cond)
{
	return 0;
}

void utablocking_init_context(lock_mutex_t * UNUSED(impl),
		      lock_context_t * UNUSED(context), int UNUSED(number))
{
}

/* New interfaces in Libutablocking */
/* set a thread is uxthread or not */
void set_ux(int is_ux)
{
	uxthread = is_ux;
}


