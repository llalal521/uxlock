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
int global_cnt_a = 0;
int global_cnt_b = 0;

void *thread_routine(void *arg)
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

int avaliable_core[] = {0, 24, 48, 72};

int main(void)
{
        pthread_t tid[THD_NUM];
        int i = 0;

        pthread_mutex_init(&global_lock_1, NULL);
        pthread_mutex_init(&global_lock_2, NULL);
        for (i = 0; i < THD_NUM; i ++)
                pthread_create(&tid[i], NULL, thread_routine, (void *)(long)i);
        for (i = 0; i < THD_NUM; i ++)
                pthread_join(tid[i], NULL);

        if (global_cnt_a != THD_NUM * TST_NUM)
                printf("FAILED!\n");
        else
                printf("SUCC!\n");

        return 0;
}

