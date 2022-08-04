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
#include <papi.h>

/* Define Platform Here */
#define x86
// #define LIBASL_INTERFACE
#ifdef r74x
#define PLAT_CPU_NUM 40
#elif defined(apple)
#define PLAT_CPU_NUM 8
#elif defined(arm)
#define PLAT_CPU_NUM 96
#else
#define PLAT_CPU_NUM 16
#endif

#define TST_NUM 1000000
#define THD_NUM 16
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

#define CRI_LONG_TYPE 6
volatile int g_max_shared_variables;

volatile int number_of_shared_variables[CRI_LONG_TYPE];
volatile uint64_t *shared_variables_memory_area[CRI_LONG_TYPE];

__thread long long tt_startp, tt_endp;
__thread int long_cri = 1;
__thread int cri_len1 = 1;
pthread_mutex_t global_lock;
int ux_short_num = 4, ux_middle_num = 2, ux_long_num = 2;
int possibility = 4;
int ratio = 0;

void access_variables(volatile uint64_t *memory_area, int number_of_variables)
{
    int i = 0;

    for (i = 0; i < number_of_variables; i++)
    {
        *(memory_area + 8 * i + 2) = *(memory_area + 8 * i + 7) + 1;
        *(memory_area + 8 * i + 7) = *(memory_area + 8 * i + 2) + 1;
    }
}

unsigned long global_cnt_short[THD_NUM] = {0};
unsigned long global_cnt_middle[THD_NUM] = {0};
unsigned long global_cnt_long[THD_NUM] = {0};

#ifdef MULTIPLE_LOCK
uta_mutex_t global_secondary_lock;
#endif

pthread_barrier_t sig_start;
volatile int global_stop = 0;
int delay, mode;
uint64_t target_latency;

#ifdef TIME
#else
#define LATENCY_RECORD 20000000
uint64_t *record_short[THD_NUM];
uint64_t *record_middle[THD_NUM];
uint64_t *record_long[THD_NUM];
__thread uint64_t record_latency_short[LATENCY_RECORD] = {0};
__thread uint64_t record_latency_middle[LATENCY_RECORD] = {0};
__thread uint64_t record_latency_long[LATENCY_RECORD] = {0};
#endif
__thread uint64_t tid;

#ifdef r74x
#define AVALIABLE_CORE_NUM 20
int avaliable_core[] = {0, 2, 4, 6, 8, 1, 3, 5, 7, 9, 10, 12, 14, 16, 18, 11, 13, 15, 17, 19};
#elif defined(apple)
#define AVALIABLE_CORE_NUM 8
int avaliable_core[] = {4, 5, 6, 7, 0, 1, 2, 3};
#elif defined(arm)
#define AVALIABLE_CORE_NUM 96
int avaliable_core[] =
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
     21, 22, 23,
     48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
     66, 67, 68, 69, 70, 71,
     24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
     42, 43, 44, 45, 46, 47,
     72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
     90, 91, 92, 93, 94, 95};
#else
#define AVALIABLE_CORE_NUM 16
int avaliable_core[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
#endif

void delay_nops(int time)
{
    for (int i = 0; i < time; i++)
    {
        NOP7;
    }
}

static inline uint32_t xor_random()
{
    static __thread uint32_t rv = 0;

    if (rv == 0)
        // rv = rand();
        rv = tid + 100;

    uint32_t v = rv;
    v ^= v << 6;
    v ^= (uint32_t)(v) >> 21;
    v ^= v << 7;
    rv = v;

    return v;
}

void short_func(int tid)
{
    tt_startp = PAPI_get_real_cyc();
    pthread_mutex_lock(&global_lock);
    tt_endp = PAPI_get_real_cyc();

    global_cnt_short[tid]++;
    access_variables(shared_variables_memory_area[0],
                     number_of_shared_variables[0]);
    pthread_mutex_unlock(&global_lock);
}

void middle_func(int tid)
{
    tt_startp = PAPI_get_real_cyc();
    pthread_mutex_lock(&global_lock);
    tt_endp = PAPI_get_real_cyc();

    global_cnt_middle[tid]++;
    access_variables(shared_variables_memory_area[2],
                     number_of_shared_variables[2]);
    pthread_mutex_unlock(&global_lock);
}

void long_func(int tid)
{
    tt_startp = PAPI_get_real_cyc();
    pthread_mutex_lock(&global_lock);
    tt_endp = PAPI_get_real_cyc();

    global_cnt_long[tid]++;
    access_variables(shared_variables_memory_area[5],
                     number_of_shared_variables[5]);
    pthread_mutex_unlock(&global_lock);
}

void *thread_routine_transparent(void *arg)
{
    uint64_t duration;
    double wait;
    int random;
    tid = (int64_t)arg;
    int record_cnt_short = 0;
    int record_cnt_middle = 0;
    int record_cnt_long = 0;
    int i = 0;

    int core_id = avaliable_core[tid % AVALIABLE_CORE_NUM];

    /* Bind core */
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core_id % PLAT_CPU_NUM, &mask);
    sched_setaffinity(0, sizeof(mask), &mask);
    sched_yield();

    record_short[tid] = record_latency_short;
    record_middle[tid] = record_latency_middle;
    record_long[tid] = record_latency_long;
    /* Start Barrier */
    pthread_barrier_wait(&sig_start);
    while (!global_stop)
    {
        i++;
        if (1)
        {
            if(tid < 4){
            /* Record Frequency */
                    short_func(tid);
                    duration = tt_endp - tt_startp;
                    record_latency_short[record_cnt_short % LATENCY_RECORD] =
                        duration;
                    record_cnt_short++;

            }
            // else if(tid < 5)
            // {
            //     middle_func(tid);
            //     duration = tt_endp - tt_startp;
            //     record_latency_middle[record_cnt_middle % LATENCY_RECORD] =
            //         duration;
            //     record_cnt_middle++;
            // }

            else
            {

                long_func(tid);
                duration = tt_endp - tt_startp;
                record_latency_long[record_cnt_long % LATENCY_RECORD] =
                    duration;
                record_cnt_long++;
            }
        }
        
        delay_nops(delay);
    }
    return NULL;
}

void print_help(void)
{
    printf("LibUxactive micro-benchmark\n");
    printf("Usage:\n");
    printf("    -h print this message\n");
    printf("    -t [thread num]\n");
    printf("    -g [number of visited shared cache lines in CS]\n");
    printf("    -d [delay between 2 acquistions]\n");
    printf("    -p [every p thread have one ux thread]n");
#ifdef HETE_EPOCH
    printf("    -r [ratio of the Long Epoch]\n");
#endif
    printf("    -T [measure time (seconds)]\n");
#ifdef LIBASL_INTERFACE
    printf("    -l [SLO in nanoseconds]\n");
#endif

#ifdef LIBUXACTIVE_INTERFACE
    printf("    -l [SLO in nanoseconds]\n");
#endif
}

int main(int argc, char *argv[])
{
    pthread_t tid[THD_NUM];
    int64_t i = 0, j = 0;
    unsigned long total_cnt = 0;
    int command;
    int sleep_time = 3;
    int nb_thread = 16;
    int cri_len = 32;
    void *(*thread_entry)(void *);

    srand(10);
    mode = 0;
    delay = 100;
    target_latency = 100000;

    while ((command = getopt(argc, argv, "m:g:s:p:d:t:hT:r:l:u:i:o:k:")) != -1)
    {
        switch (command)
        {
        case 'h':
            print_help();
            exit(0);
        case 't':
            nb_thread = atoi(optarg);
            break;
        case 'd':
            delay = atoi(optarg);
            break;
        case 'T':
            sleep_time = atoi(optarg);
            break;
        case 'p':
            possibility = atoi(optarg);
            break;
        case 'l':
            target_latency = atoi(optarg);
            break;
        case 'u':
            ux_short_num = atoi(optarg);
            break;
        case 'i':
            ux_middle_num = atoi(optarg);
            break;
        case 'o':
            ux_long_num = atoi(optarg);
            break;
        default:
        case '?':
            printf("unknown option:%s\n", optarg);
            break;
        }
    }
    number_of_shared_variables[0] = 64;
    shared_variables_memory_area[0] =
            (uint64_t *)malloc(number_of_shared_variables[0] * 64);

    number_of_shared_variables[5] = 1024;
     shared_variables_memory_area[5] =
            (uint64_t *)malloc(number_of_shared_variables[5] * 64);

    const pthread_mutexattr_t *att;
    pthread_mutex_init(&global_lock, 0);

    switch (mode)
    {
    case 0:
        thread_entry = thread_routine_transparent;
        break;
    default:
        printf("Unknown mode!\n");
        exit(-1);
        break;
    }

    pthread_barrier_init(&sig_start, 0, nb_thread + 1);
    for (i = 0; i < nb_thread; i++)
        pthread_create(&tid[i], NULL, thread_entry, (void *)i);

    sched_yield();
    pthread_barrier_wait(&sig_start);
    /* Start Barrier */
    sleep(sleep_time);
    global_stop = 1;
    int cur_short[17] = {0}, cur_middle[17] = {0}, cur_long[17] = {0};
    /* Stop Signal */
    uint64_t res = 0;
    for (i = 0; i < nb_thread; i++)
        total_cnt += global_cnt_short[i] + global_cnt_middle[i] + global_cnt_long[i];

    printf("%ld\n", total_cnt);
    for (i = 0; i < nb_thread; i++)
    {
        cur_short[0] += global_cnt_short[i];
        cur_middle[0] += global_cnt_middle[i];
        cur_long[0] += global_cnt_long[i];
        cur_short[i + 1] = global_cnt_short[i];
        cur_middle[i + 1] = global_cnt_middle[i];
        cur_long[i + 1] = global_cnt_long[i];
    }
    printf("short 1 cnt %lu\n", cur_short[0]);

    for (int i = 0; i < THD_NUM; i++){
        for (int j = 0; j < cur_short[i + 1]; j++)
        {
            printf("%lu\n", record_short[i][j]);
        }
    }
        

    printf("middle 1 cnt %lu\n", cur_middle[0]);
    for (int i = 0; i < THD_NUM; i++){
        for (int j = 0; j < cur_middle[i + 1]; j++)
        {
            printf("%lu\n", record_middle[i][j]);
        }
    }
        

    printf("long 1 cnt %lu\n", cur_long[0]);
    for (int i = 0; i < THD_NUM; i++)
        for (int j = 0; j < cur_long[i + 1]; j++)
        {
            printf("%lu\n", record_long[i][j]);
        }
    return 0;
}
