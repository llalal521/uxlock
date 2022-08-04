#ifndef __TOPOLOGY_H__
#define __TOPOLOGY_H__

#define NUMA_NODES                        1
#define CPU_NUMBER                        16
#define L_CACHE_LINE_SIZE                 128
#define PAGE_SIZE                         4096
#define CPU_FREQ                          2.5

#define x86

static inline int judge_big_core(int core_id)
{
    return 1;
}

static inline int judge_numa_node(int core_id)
{
    return 1;
}


#endif // __TOPOLOGY_H__