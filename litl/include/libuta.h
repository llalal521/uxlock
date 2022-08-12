#pragma once
#include <stdint.h>

#define UX_PRIORITY
#define CS_PRIORITY


void set_starttime();
void set_endtime();
void set_cs(int cri_len);
void set_ux(int is_ux);
int epoch_start(int epoch_id);
int epoch_end(int epoch_id, uint64_t required_latency);
