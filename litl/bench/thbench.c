#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <topology.h>

#ifdef r74x
#define PLAT_CPU_NUM 40
#define AVALIABLE_CORE_NUM 40
#elif defined apple
#define PLAT_CPU_NUM 8
#define AVALIABLE_CORE_NUM 8
#elif defined arm
#define PLAT_CPU_NUM 96
#define AVALIABLE_CORE_NUM 96
#elif defined L16G7
#define PLAT_CPU_NUM 5
#else
#define PLAT_CPU_NUM 16
#define AVALIABLE_CORE_NUM 16
#endif

#define CACHE_LINE_SIZE 64

#define r_align(n, r) (((n) + (r)-1) & -(r))
#define cache_align(n) r_align(n, CACHE_LINE_SIZE)
#define pad_to_cache_line(n) (cache_align(n) - (n))

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

#define MAX_CORE 64
#define FAIRCHECK
#define ADDNUM 1

#define MODE_NOP 0
#define MODE_ADD 1
#define LOG_SIZE 100000000

typedef enum _access_order {
	AO_SEQUENTIAL,
	AO_DEPENDENT,
	AO_RANDOM,
	AO_CUSTOM_RANDOM
} access_order_t;

static unsigned int nb_threads = 10;
pthread_barrier_t sig_start;
pthread_mutex_t lock_A;
pthread_mutex_t lock_B;
pthread_mutex_t lock_C;
pthread_mutex_t lock_D;
volatile uint64_t *g_shared_variables_memory_area_A;
volatile uint64_t *g_shared_variables_memory_area_B;
volatile uint64_t *g_shared_variables_memory_area_C;
volatile uint64_t *g_shared_variables_memory_area_D;
int delay;
int possibility = 200;

volatile int g_number_of_shared_variables;
volatile int l_number_of_shared_variables;
typedef char cache_line_t[CACHE_LINE_SIZE];

uint64_t *global_cnt;
uint64_t *global_cnt_0;
uint64_t *global_cnt_1;
uint64_t *global_cnt_2;
uint64_t *global_cnt_3;
uint64_t *global_cnt_4;
int mode = 2;

/* Tools */

void *cache_allocate(size_t n)
{
	void *res = 0;
	if ((posix_memalign((void **)&res, CACHE_LINE_SIZE, cache_align(n)) < 0)
	    || !res)
		printf("posix_memalign(%llu, %llu)", (unsigned long long)n,
		       (unsigned long long)cache_align(n));
	return res;
}

void access_variables(volatile uint64_t * memory_area, int number_of_variables)
{
	int i = 0;

	for (i = 0; i < number_of_variables; i++) {
		*(memory_area + 8 * i + 2) = *(memory_area + 8 * i + 7) + 1;
		*(memory_area + 8 * i + 7) = *(memory_area + 8 * i + 2) + 1;
	}
}

__thread uint64_t tid;

static inline uint32_t xor_random()
{
	static __thread uint32_t rv = 0;

	if (rv == 0)
		// rv = rand();
		rv = tid + 100;

	uint32_t v = rv;
	v ^= v << 6;
	v ^= (uint32_t) (v) >> 21;
	v ^= v << 7;
	rv = v;

	return v;
}

int node_aff = 0;
int cnt = 0;
uint64_t global_cnt_numa[NUMA_NODES] = { 0 };

void *thread_routine_mode_nop(void *arg)
{
	int ldelay = delay;
	double wait;
	int prevType;
	void *local_variables = arg;
	uint64_t core_id = *(uint64_t *) arg;
	tid = core_id;
	int count = 0;
	uint64_t local_possibility = possibility * 10;

	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core_id % PLAT_CPU_NUM, &mask);
	sched_setaffinity(0, sizeof(mask), &mask);
	sched_yield();

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &prevType);

	pthread_barrier_wait(&sig_start);
	if (mode == 0) {
		while (1) {
			/* r74x motivation case */
			if (judge_numa_node(core_id) == 0) {
				if (core_id == 0) {
					pthread_mutex_lock(&lock_A);
					pthread_mutex_lock(&lock_B);
					*global_cnt_1 = *global_cnt_1 + 1;
					access_variables
					    (g_shared_variables_memory_area_A,
					     g_number_of_shared_variables);
					access_variables
					    (g_shared_variables_memory_area_B,
					     g_number_of_shared_variables);
					access_variables(local_variables,
							 l_number_of_shared_variables);
					pthread_mutex_unlock(&lock_B);
					pthread_mutex_unlock(&lock_A);
				} else {
					pthread_mutex_lock(&lock_A);
					*global_cnt_0 = *global_cnt_0 + 1;
					access_variables
					    (g_shared_variables_memory_area_A,
					     g_number_of_shared_variables);
					access_variables(local_variables,
							 l_number_of_shared_variables);
					pthread_mutex_unlock(&lock_A);
				}
			} else {
				pthread_mutex_lock(&lock_B);
				*global_cnt_2 = *global_cnt_2 + 1;
				access_variables
				    (g_shared_variables_memory_area_B,
				     g_number_of_shared_variables);
				access_variables(local_variables,
						 l_number_of_shared_variables);
				pthread_mutex_unlock(&lock_B);
			}
			wait = ldelay;
			while (wait--)
				NOP7;
		}
	} else if (mode == 1) {
		while (1) {
			pthread_mutex_lock(&lock_A);
			global_cnt_numa[judge_numa_node(core_id)]++;
			*global_cnt_1 = *global_cnt_1 + 1;
			access_variables
			    (g_shared_variables_memory_area_A,
			     g_number_of_shared_variables);
			access_variables(local_variables,
					 l_number_of_shared_variables);
			pthread_mutex_unlock(&lock_A);

			wait = ldelay;
			while (wait--)
				NOP7;
		}
	} else if (mode == 2) {
		while (1) {
			unsigned random = xor_random();
			if (judge_numa_node(core_id) == node_aff
			    && random % 1000 < local_possibility) {
				pthread_mutex_lock(&lock_A);
				pthread_mutex_lock(&lock_B);
				*global_cnt_0 = *global_cnt_0 + 1;
				access_variables
				    (g_shared_variables_memory_area_A,
				     g_number_of_shared_variables);
				access_variables
				    (g_shared_variables_memory_area_B,
				     g_number_of_shared_variables);
				if (cnt++ > 10000) {
					cnt = 0;
					node_aff = (node_aff + 1) % NUMA_NODES;
				}
				pthread_mutex_unlock(&lock_B);
				pthread_mutex_unlock(&lock_A);
				wait = ldelay;
				while (wait--)
					NOP7;
				continue;
			}
			random = xor_random();
			if (random % 50 < 25) {
				pthread_mutex_lock(&lock_A);
				global_cnt_numa[judge_numa_node(core_id)]++;
				*global_cnt_1 = *global_cnt_1 + 1;
				access_variables
				    (g_shared_variables_memory_area_A,
				     g_number_of_shared_variables);
				access_variables(local_variables,
						 l_number_of_shared_variables);
				pthread_mutex_unlock(&lock_A);
			} else {
				pthread_mutex_lock(&lock_B);
				*global_cnt_2 = *global_cnt_2 + 1;
				access_variables
				    (g_shared_variables_memory_area_B,
				     g_number_of_shared_variables);
				access_variables(local_variables,
						 l_number_of_shared_variables);
				pthread_mutex_unlock(&lock_B);
			}
			wait = ldelay;
			while (wait--)
				NOP7;
		}
	}
	return 0;
}

void print_help()
{
	printf("Lock throughput benchmark\n");
	printf("Usage:\n");
	printf("    -h print this message\n");
	printf("    -t [thread num]\n");
	printf("    -g [number of visited shared cache lines in CS]\n");
	printf("    -l [number of visited local cache lines in CS]\n");
	printf("    -d [delay between 2 acquisitions]\n");
	printf("    -T [time (in seconds)]\n");
	printf("    -P [Possibility of multi-lock]\n");
}

#ifdef r74x
// int avaliable_core[] = { 0, 2, 4, 6, 8, 1, 3, 5, 7, 9, 10, 12, 14, 16, 18, 11, 13, 15, 17, 19 }; 
int avaliable_core[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18,
	20, 22, 24, 26, 28, 30, 32, 34, 36, 38,
	1, 3, 5, 7, 9, 11, 13, 15, 17, 19,
	21, 23, 25, 27, 29, 31, 33, 35, 37, 39
};
#elif defined apple
int avaliable_core[] = { 4, 5, 6, 7, 0, 1, 2, 3 };
#elif defined arm
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
int avaliable_core[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
#endif

int main(int argc, char **argv)
{
	int i = 0;
	struct timespec tt_start, tt_end;
	uint64_t start, finish;
	uint64_t *local_ptr;
	double duration, ops_per_sec;
	pthread_t *tid;
	int core_num = 0;
	int command;
	int sleep_time;
	void *(*thread_entry)(void *);
	uint64_t result;

	// default value
	delay = 0;
	nb_threads = 5;
	sleep_time = 4;

	while ((command = getopt(argc, argv, "t:g:d:l:hT:P:M:")) != -1) {
		switch (command) {
		case 'h':
			print_help();
			exit(0);
			break;
		case 't':
			nb_threads = atoi(optarg);
			break;
		case 'g':
			g_number_of_shared_variables = atoi(optarg);
			break;
		case 'l':
			l_number_of_shared_variables = atoi(optarg);
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'T':
			sleep_time = atoi(optarg);
			break;
		case 'P':
			possibility = atoi(optarg);
			break;
		case 'M':
			mode = atoi(optarg);
			break;
		case '?':
			printf("unknown option:%s\n", optarg);
			break;
		}
	}

	thread_entry = thread_routine_mode_nop;

	global_cnt = (uint64_t *) cache_allocate(sizeof(uint64_t));
	*global_cnt = 0;

	global_cnt_0 = (uint64_t *) cache_allocate(sizeof(uint64_t));
	*global_cnt_0 = 0;

	global_cnt_1 = (uint64_t *) cache_allocate(sizeof(uint64_t));
	*global_cnt_1 = 0;

	global_cnt_2 = (uint64_t *) cache_allocate(sizeof(uint64_t));
	*global_cnt_2 = 0;

	global_cnt_3 = (uint64_t *) cache_allocate(sizeof(uint64_t));
	*global_cnt_3 = 0;

	global_cnt_4 = (uint64_t *) cache_allocate(sizeof(uint64_t));
	*global_cnt_4 = 0;

	g_shared_variables_memory_area_A =
	    (uint64_t *) cache_allocate(g_number_of_shared_variables *
					sizeof(cache_line_t));
	g_shared_variables_memory_area_B =
	    (uint64_t *) cache_allocate(g_number_of_shared_variables *
					sizeof(cache_line_t));
	g_shared_variables_memory_area_C =
	    (uint64_t *) cache_allocate(g_number_of_shared_variables *
					sizeof(cache_line_t));
	g_shared_variables_memory_area_D =
	    (uint64_t *) cache_allocate(g_number_of_shared_variables *
					sizeof(cache_line_t));
	tid = (pthread_t *) malloc(sizeof(pthread_t) * nb_threads);
	pthread_mutex_init(&lock_A, NULL);
	pthread_mutex_init(&lock_B, NULL);
	pthread_mutex_init(&lock_C, NULL);
	pthread_mutex_init(&lock_D, NULL);
	pthread_barrier_init(&sig_start, 0, nb_threads + 1);
	for (i = 0; i < nb_threads; i++) {
		local_ptr =
		    (uint64_t *) cache_allocate(l_number_of_shared_variables *
						sizeof(cache_line_t));
		*local_ptr = (uint64_t) avaliable_core[i % AVALIABLE_CORE_NUM];
		pthread_create(&tid[i], NULL, thread_entry, local_ptr);
		core_num++;
	}

	pthread_barrier_wait(&sig_start);

	// start throughput test
	clock_gettime(CLOCK_MONOTONIC, &tt_start);
	sleep(sleep_time);
	clock_gettime(CLOCK_MONOTONIC, &tt_end);
	// end throughput test

	// local_global_cnt = *global_cnt;
	result =
	    *global_cnt_0 + *global_cnt_1 + *global_cnt_2 + *global_cnt_3 +
	    *global_cnt_4;

	start = (tt_start.tv_sec * 1000000000LL) + tt_start.tv_nsec;
	finish = (tt_end.tv_sec * 1000000000LL) + tt_end.tv_nsec;
	duration = (double)(finish - start) / 1000000000LL;
	ops_per_sec = (double)(result) / duration;

	printf("%.4lf\n", (double)ops_per_sec);
	// printf("%lu\n", *global_cnt_0 + *global_cnt_1 + *global_cnt_2);
	printf("%lu\n%lu\n%lu\n%lu\n%lu\n", *global_cnt_0, *global_cnt_1,
	       *global_cnt_2, *global_cnt_3, *global_cnt_4);
	for (int i = 0; i < NUMA_NODES; i++) {
		printf("Node %d %lu\n", i, global_cnt_numa[i]);
	}
	return 0;
}
