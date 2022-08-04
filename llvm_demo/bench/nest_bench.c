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
#include "../topology.h"
#include <papi.h>
#include "../uta.h"
#define THD_NUM 16

#define PLAT_CPU_NUM 16
#define AVALIABLE_CORE_NUM	16
int avaliable_core[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
// #endif

#ifdef	LIBUXACTIVE_INTERFACE
#include "libuxactive.h"
#endif

#ifdef	LIBUTA_INTERFACE
#include "libuta.h"
#endif

#ifdef	LIBUXSHFL_INTERFACE
#include "libuxshfl.h"
#endif

#ifdef	LIBUXPICK_INTERFACE
#include "libuxpick.h"
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
lock_transparent_mutex_t * lock_A;
lock_transparent_mutex_t * lock_B;
lock_transparent_mutex_t * lock_C;
lock_transparent_mutex_t * lock_D;
volatile uint64_t *g_shared_variables_memory_area_A;
volatile uint64_t *g_shared_variables_memory_area_B;
volatile uint64_t *g_shared_variables_memory_area_C;
volatile uint64_t *g_shared_variables_memory_area_D;
int delay;
int possibility = 1;

volatile int g_number_of_shared_variables = 10;
volatile int l_number_of_shared_variables;
typedef char cache_line_t[CACHE_LINE_SIZE];

__thread long long tt_startp, tt_endp;
uint64_t *record_all[THD_NUM];
#define LATENCY_RECORD 100000000
__thread uint64_t record_latency[LATENCY_RECORD] = { 0 };
__thread uint64_t record_cnt;
uint64_t *global_cnt[THD_NUM][5];
uint64_t *global_cnt_0[THD_NUM];
uint64_t *global_cnt_1[THD_NUM];
uint64_t *global_cnt_2[THD_NUM];
uint64_t *global_cnt_3[THD_NUM];
uint64_t *global_cnt_4[THD_NUM];
int mode = 2;

void delay_nops(int time) {
    for (int i = 0; i < time; i ++) {
        NOP7;
    }
}

/* Tools */



void access_variables(volatile uint64_t * memory_area, int number_of_variables)
{
	int i = 0;

	for (i = 0; i < number_of_variables; i++) {
		*(memory_area + 8 * i + 2) = *(memory_area + 8 * i + 7) + 1;
		*(memory_area + 8 * i + 7) = *(memory_area + 8 * i + 2) + 1;
	}
}

__thread uint64_t tid;

void *cache_allocate(size_t n)
{
	void *res = 0;
	if ((posix_memalign((void **)&res, CACHE_LINE_SIZE, cache_align(n)) < 0)
	    || !res)
		printf("posix_memalign(%llu, %llu)", (unsigned long long)n,
		       (unsigned long long)cache_align(n));	   
	return res;
}

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

void *thread_routine_mode_nop(void *arg)
{
	int ldelay = delay;
	double wait;
	int prevType;
	void *local_variables = arg;
	uint64_t core_id = *(uint64_t *) arg;
	tid = core_id;
	int count = 0;
	uint64_t duration;
	uint64_t local_possibility = possibility * 10;
	int type;

#ifdef UX_PRIORITY
	if(tid / 4 == 0)
        set_ux(1);
	else
        set_ux(0);
#endif

	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core_id % PLAT_CPU_NUM, &mask);
	sched_setaffinity(0, sizeof(mask), &mask);
	sched_yield();
    
	record_all[tid] = record_latency;
	
	/* Half of the core in node lock b, other half lock a */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &prevType);
    
	global_cnt[tid][0] = global_cnt_0[tid];
	global_cnt[tid][1] = global_cnt_1[tid];
	global_cnt[tid][2] = global_cnt_2[tid];
	global_cnt[tid][3] = global_cnt_3[tid];
	global_cnt[tid][4] = global_cnt_4[tid];
	pthread_barrier_wait(&sig_start);
    if (mode == 2) {
		while (1) {
			unsigned random = xor_random();
			if(tid / 4 == 0){
				if(random % 1000 < local_possibility){
					tt_startp =  PAPI_get_real_cyc();
					uta_mutex_lock(lock_A->lock_lock, &lock_A->lock_node[tid]);
					tt_endp =  PAPI_get_real_cyc();
					*global_cnt_0[tid] = *global_cnt_0[tid] + 1;
					access_variables
				    	(g_shared_variables_memory_area_A,
				    	 g_number_of_shared_variables);
					access_variables
				    	(g_shared_variables_memory_area_B,
				    	 g_number_of_shared_variables);
					uta_mutex_unlock(lock_A->lock_lock, &lock_A->lock_node[tid]);
					duration =  tt_endp - tt_startp;
            		record_latency[record_cnt % LATENCY_RECORD] = duration;
            		record_cnt++;
					goto out1;
				}
				
				uta_mutex_lock(lock_B->lock_lock, &lock_B->lock_node[tid]);
				
				*global_cnt_1[tid] = *global_cnt_1[tid] + 1;
				access_variables
				    (g_shared_variables_memory_area_A,
				     g_number_of_shared_variables);
				access_variables
				    (g_shared_variables_memory_area_B,
				     g_number_of_shared_variables);
				uta_mutex_unlock(lock_B->lock_lock, &lock_B->lock_node[tid]);
				
			}
			else{
				uta_mutex_lock(lock_A->lock_lock, &lock_A->lock_node[tid]);
				uta_mutex_lock(lock_B->lock_lock, &lock_B->lock_node[tid]);
				*global_cnt_3[tid] = *global_cnt_3[tid] + 1;
				access_variables
				    (g_shared_variables_memory_area_A,
				     g_number_of_shared_variables);
				access_variables
				    (g_shared_variables_memory_area_B,
				     g_number_of_shared_variables);
				uta_mutex_unlock(lock_B->lock_lock, &lock_B->lock_node[tid]);
				uta_mutex_unlock(lock_A->lock_lock, &lock_A->lock_node[tid]);
			}
out1:
			delay_nops(delay);
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

#ifdef HMCS_SET_RELEASE
void set_reorder_threshold(int val);
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
	int num = 0;
	int command;
	int sleep_time;
	void *(*thread_entry)(void *);
	uint64_t result[THD_NUM];
	int release = 1000;

	// default value
	delay = 0;
	nb_threads = 16;
	sleep_time = 4;

	while ((command = getopt(argc, argv, "t:g:d:l:hT:P:M:R:")) != -1) {
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
		case 'R':
			release = atoi(optarg);
			break;
		case '?':
			printf("unknown option:%s\n", optarg);
			break;
		}
	}

	thread_entry = thread_routine_mode_nop;

#ifdef HMCS_SET_RELEASE
	set_reorder_threshold(release);
#endif
	
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
	for(int i = 0; i < THD_NUM; i++){
		global_cnt_0[i] = (uint64_t *) cache_allocate(sizeof(uint64_t));
		*global_cnt_0[i] = 0;

		global_cnt_1[i] = (uint64_t *) cache_allocate(sizeof(uint64_t));
		*global_cnt_1[i] = 0;

		global_cnt_2[i] = (uint64_t *) cache_allocate(sizeof(uint64_t));
		*global_cnt_2[i] = 0;

		global_cnt_3[i] = (uint64_t *) cache_allocate(sizeof(uint64_t));
		*global_cnt_3[i] = 0;

		global_cnt_4[i] = (uint64_t *) cache_allocate(sizeof(uint64_t));
		*global_cnt_4[i] = 0;
	}

	tid = (pthread_t *) malloc(sizeof(pthread_t) * nb_threads);
    lock_A = lock_create(0);
    lock_B = lock_create(0);
    lock_C = lock_create(0);
    lock_D = lock_create(0);
	pthread_barrier_init(&sig_start, 0, nb_threads + 1);
	for (i = 0; i < nb_threads; i++) {
		local_ptr =
		    (uint64_t *) cache_allocate(l_number_of_shared_variables *
						sizeof(cache_line_t));
		*local_ptr = (uint64_t) avaliable_core[i % THD_NUM];
		pthread_create(&tid[i], NULL, thread_entry, local_ptr);
		core_num++;
	}
	pthread_barrier_wait(&sig_start);

	// start throughput test
	clock_gettime(CLOCK_MONOTONIC, &tt_start);
	sleep(sleep_time);
	clock_gettime(CLOCK_MONOTONIC, &tt_end);

	for (i = 0; i < nb_threads; i++){
		result[i] = *global_cnt[i][0] ;
		num += result[i];
	}
    printf("utal\n");
	printf("%d\n", num);
	for (i = 0; i < nb_threads; i++) {
        printf("core %ld cnt %d\n", i, result[i]);
        for (int j = 0; j < result[i]; j++){
           printf("%lu\n", record_all[i][j]);
        }
    }
	return 0;
}
