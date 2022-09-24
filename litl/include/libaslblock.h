#pragma once
#include <stdint.h>


int epoch_start(int epoch_id);
int epoch_end(int epoch_id, uint64_t required_latency);
