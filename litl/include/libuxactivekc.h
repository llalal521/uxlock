#pragma once
#include <stdint.h>

#define UX_PRIORITY
int epoch_start(int epoch_id);
int epoch_end(int epoch_id, uint64_t required_latency);
void set_ux(int is_ux);