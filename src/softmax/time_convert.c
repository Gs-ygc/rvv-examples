













#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "time_convert.h"

// 定义全局变量
double g_time_to_cycle_ratio = 0.0;
uint64_t g_timebase_freq = 0;
uint64_t g_cpu_freq = 0;

// 读取设备树属性
static uint64_t get_dt(const char * path) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open timebase-frequency");
        return -1; // 默认值 24 MHz
    }

    uint32_t value;
    ssize_t bytes_read = read(fd, &value, sizeof(value));
    close(fd);

    if (bytes_read != sizeof(value)) {
        printf("Failed to read timebase-frequency, return -1\n");
        return -1;
    }

    // 设备树使用大端序，需要转换
    value = be32toh(value);
    printf("Actual timebase-frequency: %u Hz (0x%08x)\n", value, value);
    return value;
}
// 获取 timebase-frequency
uint64_t get_timebase_frequency(void) {
    // 尝试不同的设备树路径
    const char *possible_paths[] = {
        "/proc/device-tree/cpus/timebase-frequency",
        "/proc/device-tree/timebase-frequency",
        "/sys/firmware/devicetree/base/cpus/timebase-frequency",
        "/sys/firmware/devicetree/base/timebase-frequency",
        NULL
    };

    for (int i = 0; possible_paths[i] != NULL; i++) {
        uint64_t freq = get_dt(possible_paths[i]);
        if (freq > 0) {
            printf("Found timebase-frequency: %lu Hz at %s\n", freq, possible_paths[i]);
            return freq;
        }
    }

    // 默认值（常见值）
    printf("Using default timebase-frequency: 10 MHz\n");
    return 10000000; // 10 MHz
}

// 获取 CPU 频率
uint64_t get_cpu_frequency_info1(void) {
    // 尝试从 /proc/cpuinfo 获取
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "cpu MHz") || strstr(line, "BogoMIPS")) {
                double mhz = 0;
                if (sscanf(line, "cpu MHz : %lf", &mhz) == 1 && mhz > 0) {
                    fclose(fp);
                    uint64_t freq = (uint64_t)(mhz * 1000000);
                    printf("Found CPU frequency: %.2f MHz (%lu Hz)\n", mhz, freq);
                    return freq;
                }
            }
        }
        fclose(fp);
    }

    // 默认值（常见值）
    printf("Using default CPU frequency: 1 GHz\n");
    return 1000000000; // 1 GHz
}

// 从 sysfs 获取当前 CPU 频率
uint64_t get_cpu_frequency(void) {
    const char *freq_path = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq";
    FILE *fp = fopen(freq_path, "r");
    if (fp == NULL) {
        perror("Failed to open cpuinfo_cur_freq");
        return 0;
    }

    uint64_t freq_khz = 0;
    if (fscanf(fp, "%lu", &freq_khz) != 1) {
        fclose(fp);
        return 0;
    }

    fclose(fp);

    // 转换为 Hz（从 kHz 转换为 Hz）
    uint64_t freq_hz = freq_khz * 1000;
    printf("Current CPU frequency: %lu kHz (%lu Hz)\n", freq_khz, freq_hz);
    return freq_hz;
}
// 读取 rdtime 寄存器（RISC-V 时间寄存器）
static inline uint64_t read_rdtime(void) {
    uint64_t time;
    asm volatile ("rdtime %0" : "=r"(time));
    return time;
}

// 将时间戳转换为 CPU 周期数
uint64_t time_to_cycles(uint64_t time_value, uint64_t timebase_freq, uint64_t cpu_freq) {
    if (timebase_freq == 0) {
        return 0;
    }
    
    // 计算时间值对应的秒数
    double seconds = (double)time_value / timebase_freq;
    
    // 转换为 CPU 周期数
    return (uint64_t)(seconds * cpu_freq);
}
// 时间转周期
uint64_t time_to_cycles_a(uint64_t time_value) {
    return (uint64_t)(time_value * g_time_to_cycle_ratio);
}

// 周期转时间
uint64_t cycles_to_time(uint64_t cycles) {
    return (uint64_t)(cycles / g_time_to_cycle_ratio);
}

// 读取 rdtime 寄存器
uint64_t get_rdtime(void) {
    uint64_t time;
    asm volatile ("rdtime %0" : "=r"(time));
    return time;
}

int init_time_to_cycles(void) {
    g_timebase_freq = get_timebase_frequency();
    g_cpu_freq = get_cpu_frequency();

    if (g_cpu_freq == 0 || g_timebase_freq == 0) {
        fprintf(stderr, "Failed to initialize time-to-cycle conversion\n");
        return -1;
    }

    g_time_to_cycle_ratio = (double)g_cpu_freq / g_timebase_freq;
    
    printf("Time-to-cycle ratio initialized: %.6f\n", g_time_to_cycle_ratio);
    printf("1 time unit = %.6f CPU cycles\n", g_time_to_cycle_ratio);
    
    return 0;
}

int test_time_to_cycles() {
    // 获取频率信息
    uint64_t timebase_freq = get_timebase_frequency();
    uint64_t cpu_freq = get_cpu_frequency();
    
    printf("Timebase frequency: %lu Hz\n", timebase_freq);
    printf("CPU frequency: %lu Hz\n", cpu_freq);
    printf("Conversion ratio: 1 time unit = %.6f CPU cycles\n", 
           (double)cpu_freq / timebase_freq);
    
    // 读取时间寄存器
    uint64_t time_value = read_rdtime();
    printf("Raw rdtime value: %lu\n", time_value);
    
    // 转换为 CPU 周期数
    uint64_t cycles = time_to_cycles(time_value, timebase_freq, cpu_freq);
    printf("Converted to CPU cycles: %lu\n", cycles);
    
    // 演示连续读取和转换
    printf("\nContinuous time measurement:\n");
    for (int i = 0; i < 5; i++) {
        uint64_t start_time = read_rdtime();
        // 模拟一些工作（比如空循环）
        for (volatile int j = 0; j < 1000000; j++);
        uint64_t end_time = read_rdtime();
        
        uint64_t time_diff = end_time - start_time;
        uint64_t cycles_diff = time_to_cycles(time_diff, timebase_freq, cpu_freq);
        
        printf("Iteration %d: Time diff = %lu, Cycles diff = %lu\n", 
               i, time_diff, cycles_diff);
    }
    
    return 0;
}
