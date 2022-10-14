#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define r74x

#ifdef r74x
#define PLAT_CPU_NUM 40
#elif defined(apple)
#define PLAT_CPU_NUM 8
#elif defined(arm)
#define PLAT_CPU_NUM 96
#else
#define PLAT_CPU_NUM 16
#endif

#define TST_NUM 100000
#define THD_NUM 40
#define RECORD_FREQ 1

#define NOP0 __asm__ __volatile__("\nnop\n");
#define NOP1 NOP0 NOP0
#define NOP2 NOP1 NOP1
#define NOP3 NOP2 NOP2
#define NOP4 NOP3 NOP3
#define NOP5 NOP4 NOP4
#define NOP6 NOP5 NOP5
#define NOP7 NOP6 NOP6
#define NOP8 NOP7 NOP7
#define NOP9 NOP8 NOP8
#define NOP10 NOP9 NOP9
#define NOP11 NOP10 NOP10
#define NOP12 NOP11 NOP11

pthread_mutex_t global_lock_1;
pthread_mutex_t global_lock_2;
pthread_rwlock_t global_rwlock;
int global_cnt_a = 0;
int global_cnt_b = 0;

#ifdef r74x
#define AVALIABLE_CORE_NUM	10
int avaliable_core[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18 };
#elif defined(apple)
#define AVALIABLE_CORE_NUM	8
int avaliable_core[] = { 4, 5, 6, 7, 0, 1, 2, 3 };
#elif defined(arm)
#define AVALIABLE_CORE_NUM	96
int avaliable_core[] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23,
	48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
	66, 67, 68, 69, 70, 71,
	24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
	42, 43, 44, 45, 46, 47,
	72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
	90, 91, 92, 93, 94, 95
};
#else
#define AVALIABLE_CORE_NUM	16
int avaliable_core[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
#endif

#ifdef	LIBUXACTIVE_INTERFACE
#include "libuxactive.h"
#endif

#ifdef	LIBUXSHFL_INTERFACE
#include "libuxshfl.h"
#endif

#ifdef	LIBUXPICK_INTERFACE
#include "libuxpick.h"
#endif

#ifdef	LIBUTA_INTERFACE
#include "libuta.h"
#endif

void *test_mutex_routine(void *arg)
{
	int tid = (int)(long)arg;
	int core_id;
	core_id = avaliable_core[tid % AVALIABLE_CORE_NUM];
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core_id, &mask);
	sched_setaffinity(0, sizeof(mask), &mask);
	sched_yield();

	for (int i = 0; i < TST_NUM; i++) {
		pthread_mutex_lock(&global_lock_2);
		global_cnt_a++;
		pthread_mutex_unlock(&global_lock_2);
	}
	return NULL;
}

void *test_rwlock_routine(void *arg)
{
	int core_id = (int)(long)arg;
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core_id, &mask);
	sched_setaffinity(0, sizeof(mask), &mask);
	sched_yield();

	/* Reader Writer Lock */
	for (int i = 0; i < TST_NUM; i++) {
		if (i % 3) {
			if (i == 1)
				pthread_rwlock_rdlock(&global_rwlock);
			else
				while (pthread_rwlock_tryrdlock(&global_rwlock)
				       != 0) ;
			if (global_cnt_a != global_cnt_b) {
				printf("RWLock test Failed!\n");
				exit(-1);
			}
			pthread_rwlock_unlock(&global_rwlock);
		} else {
			if (global_cnt_a % 2)
				pthread_rwlock_wrlock(&global_rwlock);
			else
				while (pthread_rwlock_trywrlock(&global_rwlock)
				       != 0) ;
			global_cnt_a++;
			global_cnt_b++;
			pthread_rwlock_unlock(&global_rwlock);
		}
		// if (i % (TST_NUM/10) == 0)
		//         printf("Alive %d!\n", i);
	}

}

int main(void)
{
	pthread_t tid[THD_NUM];
	int i = 0;
	pthread_mutex_init(&global_lock_1, NULL);
	pthread_mutex_init(&global_lock_2, NULL);
	for (i = 0; i < THD_NUM; i++)
		pthread_create(&tid[i], NULL, test_mutex_routine,
			       (void *)(long)i);
	for (i = 0; i < THD_NUM; i++)
		pthread_join(tid[i], NULL);

	if (global_cnt_a != THD_NUM * TST_NUM) {
		printf("Mutex FAILED!\n");
		exit(-1);
	} else {
		printf("Mutex SUCC!\n");
	}

	// global_cnt_a = 0;
	// global_cnt_b = 0;
	// pthread_rwlock_init(&global_rwlock, NULL);
	// for (i = 0; i < THD_NUM; i ++)
	//         pthread_create(&tid[i], NULL, test_rwlock_routine, (void *)(long)i);
	// for (i = 0; i < THD_NUM; i ++)
	//         pthread_join(tid[i], NULL);

	// if (global_cnt_a != global_cnt_b) {
	//         printf("RWLock FAILED!\n");
	//         exit (-1);
	// } else {
	//         printf("RWLock SUCC!\n");
	// }
	return 0;
}
