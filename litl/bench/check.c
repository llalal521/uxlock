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

#define TST_NUM 10000000
#define THD_NUM 12

pthread_mutex_t global_lock_1;
pthread_mutex_t global_lock_2;
pthread_rwlock_t global_rwlock;
int global_cnt_a = 0;
int global_cnt_b = 0;

void *test_mutex_routine(void *arg)
{
        int core_id = (int)(long)arg;
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(core_id, &mask);
        sched_setaffinity(0, sizeof(mask), &mask);
        sched_yield();

        for (int i = 0; i < TST_NUM; i++) {
                if (core_id % 2 == 0) {
                        pthread_mutex_lock(&global_lock_1);
                        pthread_mutex_lock(&global_lock_2);
                        global_cnt_a = global_cnt_b + 1;
                        global_cnt_b = global_cnt_a;
                        pthread_mutex_unlock(&global_lock_2);
                        pthread_mutex_unlock(&global_lock_1);
                } else {
                        pthread_mutex_lock(&global_lock_2);
                        global_cnt_a = global_cnt_b + 1;
                        global_cnt_b = global_cnt_a;
                        pthread_mutex_unlock(&global_lock_2);
                }
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
				while (pthread_rwlock_tryrdlock(&global_rwlock) != 0);
			if(global_cnt_a != global_cnt_b) {
                                printf("RWLock test Failed!\n");
                                exit(-1);
                        }
			pthread_rwlock_unlock(&global_rwlock);
		} else {
			if (global_cnt_a % 2)
				pthread_rwlock_wrlock(&global_rwlock);
			else
				while (pthread_rwlock_trywrlock(&global_rwlock) != 0);
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
        for (i = 0; i < THD_NUM; i ++)
                pthread_create(&tid[i], NULL, test_mutex_routine, (void *)(long)i);
        for (i = 0; i < THD_NUM; i ++)
                pthread_join(tid[i], NULL);

        if (global_cnt_a != THD_NUM * TST_NUM) {
                printf("Mutex FAILED!\n");
                exit (-1);
        } else {
                printf("Mutex SUCC!\n");
        }

        global_cnt_a = 0;
        global_cnt_b = 0;
        pthread_rwlock_init(&global_rwlock, NULL);
        for (i = 0; i < THD_NUM; i ++)
                pthread_create(&tid[i], NULL, test_rwlock_routine, (void *)(long)i);
        for (i = 0; i < THD_NUM; i ++)
                pthread_join(tid[i], NULL);

        if (global_cnt_a != global_cnt_b) {
                printf("RWLock FAILED!\n");
                exit (-1);
        } else {
                printf("RWLock SUCC!\n");
        }
        return 0;
}

