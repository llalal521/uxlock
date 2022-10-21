#define L_CACHE_LINE_SIZE 64
// #include <topology.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <papi.h>

#define r_align(n, r) (((n) + (r)-1) & -(r))
#define cache_align(n) r_align(n, L_CACHE_LINE_SIZE)
#define pad_to_cache_line(n) (cache_align(n) - (n))
#define MEMALIGN(ptr, alignment, size) 
void access_variables(volatile uint64_t * memory_area, int number_of_variables)
{
	int i = 0;

	for (i = 0; i < number_of_variables; i++) {
		*(memory_area + 8 * i + 2) = *(memory_area + 8 * i + 7) + 1;
		*(memory_area + 8 * i + 7) = *(memory_area + 8 * i + 2) + 1;
	}
}    
// struct utamutexee_lock {
// 	union {
// 		volatile unsigned u;
// 		struct {
// 			volatile unsigned char locked;
// 			volatile unsigned char contended;
// 		} b;
// 	} l;
// 	uint8_t padding[4];
// 	/* uint8_t padding_cl[56]; */

// 	unsigned int n_spins;
// 	unsigned int n_spins_unlock;
// 	size_t n_acq;
// 	unsigned int n_miss;
// 	unsigned int n_miss_limit;
// 	unsigned int is_futex;
// 	unsigned int n_acq_first_sleep;
// 	unsigned int retry_spin_every;
// 	unsigned int padding3;
// 	uint8_t padding2[CACHE_LINE_SIZE - 6 * sizeof(size_t)];
// } utamutexee_mutex_t;

static uint64_t __always_inline rdtscp(void)
{
	uint32_t a, d;
	__asm __volatile("rdtscp; mov %%eax, %0; mov %%edx, %1; cpuid"
			 : "=r" (a), "=r" (d)
			 : : "%rax", "%rbx", "%rcx", "%rdx");
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}


int main() {
    // utamutexee_mutex_t *impl =
	//     (utamutexee_mutex_t *) alloc_cache_align(sizeof(utamutexee_mutex_t));
    // printf("size_t %d\n", pad_to_cache_line(sizeof(int)));
    int short_number_of_shared_variables = 512;
    volatile uint64_t *short_shared_variables_memory_area;
    short_shared_variables_memory_area =
	    (uint64_t *) malloc(short_number_of_shared_variables * 64);
    while(1) {
        uint64_t start = PAPI_get_real_cyc();
        access_variables(short_shared_variables_memory_area,
				 short_number_of_shared_variables);
        uint64_t duration = PAPI_get_real_cyc() - start;
        printf("duration %lu\n", duration);
    }

    
    return 0;
}