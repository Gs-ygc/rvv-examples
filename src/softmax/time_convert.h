#ifndef TIME_CONVERT_H
#define TIME_CONVERT_H

#include <stdint.h>

// 声明全局变量
extern double g_time_to_cycle_ratio;
extern uint64_t g_timebase_freq;
extern uint64_t g_cpu_freq;

// 函数声明
int init_time_to_cycles(void);
uint64_t time_to_cycles_a(uint64_t time_value);
uint64_t cycles_to_time(uint64_t cycles);
uint64_t get_rdtime(void);

#endif // TIME_CONVERT_H
