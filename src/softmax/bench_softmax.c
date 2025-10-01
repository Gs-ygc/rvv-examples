// file: bench_matrix_transpose.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <bench_softmax_utils.h>
#include <unistd.h>
#include <fcntl.h>

/** Declaring various softmax implementation benchmarks **/
softmax_bench_result_t softmax_baseline_fp32_bench(float* dst, float* src, double* golden, size_t n);

softmax_bench_result_t softmax_rvv_norm_fp32_bench(float* dst, float* src, double* golden, size_t n); 

softmax_bench_result_t softmax_rvv_fp32_bench(float* dst, float* src, double* golden, size_t n); 

softmax_bench_result_t softmax_stable_rvv_fp32_bench(float* dst, float* src, double* golden, size_t n);

softmax_bench_result_t softmax_scalar_quick_dirty_expf_fp32_bench(float* dst, float* src, double* golden, size_t n);

typedef softmax_bench_result_t (softmax_bench_func_t)(float* dst, float* src, double* golden, size_t n);

/** Descriptor structure for softmax benchmark */
typedef struct {
    softmax_bench_func_t* bench;
    softmax_bench_result_t result;
    char label[100];
} softmax_bench_t;


extern void softmax_golden_fp32_fp64(double* dst, float* src, size_t n);

#ifndef NUM_TESTS
#define NUM_TESTS 100
#endif


/**
 * @brief 设置 kernel.perf_user_access 为 2
 * @return 0 成功，-1 失败
 */
int set_perf_user_access(void) {
    const char *path = "/proc/sys/kernel/perf_user_access";
    const char *value = "2";
    int fd;

    fd = open(path, O_WRONLY);
    if (fd == -1) {
        perror("Failed to open /proc/sys/kernel/perf_user_access");
        return -1;
    }

    if (write(fd, value, strlen(value)) == -1) {
        perror("Failed to write to /proc/sys/kernel/perf_user_access");
        close(fd);
        return -1;
    }

    close(fd);
    printf("Successfully set kernel.perf_user_access = %s\n", value);
    return 0;
}

#include "time_convert.h"

int main(void) {
    int i;
    softmax_bench_t benchmarks[] = {
        (softmax_bench_t){.bench = softmax_baseline_fp32_bench,                .label="baseline n-element softmax"},
        (softmax_bench_t){.bench = softmax_scalar_quick_dirty_expf_fp32_bench, .label="scalar quick_dirty_expf n-element softmax"},
        (softmax_bench_t){.bench = softmax_rvv_norm_fp32_bench,                .label="rvv-based (norm only) n-element softmax"},
        (softmax_bench_t){.bench = softmax_rvv_fp32_bench,                     .label="rvv-based n-element softmax"},
        (softmax_bench_t){.bench = softmax_stable_rvv_fp32_bench,              .label="rvv-based n-element stable softmax"},
    };
#if defined(COUNT_CYCLE)
    if (set_perf_user_access() != 0) {
        return EXIT_FAILURE;
    }
    do{
    	uint64_t cycle;
    	asm volatile ("rdcycle %0" : "=r"(cycle));
	printf("Cycle = %ld", cycle);
    }while(0);
#endif
     if (init_time_to_cycles() != 0) {
        fprintf(stderr, "Time conversion initialization failed\n");
        return EXIT_FAILURE;
    }
    do{
	uint64_t time_val = get_rdtime();
    	uint64_t cycles = time_to_cycles_a(time_val);

    	printf("Current time: %lu\n", time_val);
    	printf("Equivalent cycles: %lu\n", cycles);

    	// 也可以直接使用全局比例
    	printf("Global ratio: %.6f\n", g_time_to_cycle_ratio);

    	// 测试转换
    	uint64_t test_time = 1000000;
    	uint64_t test_cycles = time_to_cycles_a(test_time);
    	printf("%lu time units = %lu cycles\n", test_time, test_cycles);
    }while(0);
    size_t testSizes[] = {4, 16, 17, 32, 33, 128, 129, 511, 512, 1024, 2048, 8192, 40960};
    for (size_t testId = 0; testId < sizeof(testSizes) / sizeof(size_t); testId++)
    {
        size_t n = testSizes[testId];
#       ifdef VERBOSE 
        printf("--------------------------------------------------------------------------------\n");
        printf("--------------------------------------------------------------------------------\n");
        printf("Benchmarking softmax on a %d-element array.\n", n);
#       endif
        float* src = malloc(n * sizeof(float));
        float* dst = malloc(n * sizeof(float));
        double* golden = malloc(n * sizeof(double));
        assert(src);
        assert(dst);
        assert(golden);

        // reset benchmark results
        for (unsigned benchId=0; benchId < sizeof(benchmarks) / sizeof(softmax_bench_t); benchId++)
        {
            benchmarks[benchId].result.max_abs_error = 0.;
            benchmarks[benchId].result.max_rel_error = 0.;
            benchmarks[benchId].result.perf_count = 0;
            benchmarks[benchId].result.error_norm2 = 0.f;
        }

        int j;
        const float RAND_LOWER_BOUND = -2.f;
        const float RAND_RANGE = 4.f;

        for (j = 0; j < NUM_TESTS; ++j) {
            // random initialization of the input arrays
            for (i = 0; i < n; ++i) {
                src[i] = RAND_RANGE * rand() / (float) RAND_MAX + RAND_LOWER_BOUND;
            }

            // computing golden value
            softmax_golden_fp32_fp64(golden, src, n);

#           ifdef VERY_VERBOSE
            printf("source matrix:\n");
            array_dump_fp32(src, n);
            printf("golden result:\n");
            array_dump_fp64(golden, n);
#           endif // VERY_VERBOSE

            // softmax benchmarks. iterating over all existing implementation for this given input set
            for (unsigned benchId=0; benchId < sizeof(benchmarks) / sizeof(softmax_bench_t); benchId++)
            {
                memset(dst, 0, sizeof(dst)); // resetting array in-between experiments

                softmax_bench_result_t local_result = benchmarks[benchId].bench(dst, src, golden, n);

                benchmarks[benchId].result = accumulate_bench_result(benchmarks[benchId].result, local_result);

#               ifdef VERY_VERBOSE
                printf("%s result:\n", benchmarks[benchId].label);
                array_dump_fp32(dst, n);
#               endif // VERY_VERBOSE

            }
        }

        // display results
        for (unsigned benchId=0; benchId < sizeof(benchmarks) / sizeof(softmax_bench_t); benchId++)
        {
            softmax_bench_result_t bench_result = benchmarks[benchId].result;
            bench_result.perf_count = bench_result.perf_count / NUM_TESTS;
            bench_result.mean_rel_error = bench_result.mean_rel_error / NUM_TESTS;
            bench_result.error_norm2 = sqrt(bench_result.error_norm2);


#           ifdef VERBOSE 
            printf("--------------------------------------------------------------------------------\n");
            printf("%s used %d " PERF_METRIC "(s) to evaluate softmax on a %d-element array.\n",
                benchmarks[benchId].label, bench_result.perf_count, n);
            printf(" " PERF_METRIC " per elements:    %.3f\n", (double) bench_result.perf_count / n);
            printf("  element(s) per " PERF_METRIC ": %.3f\n", (double) n / bench_result.perf_count);
            printf("  max absolute error:  %.4a\n", bench_result.max_abs_error);
            printf("  max relative error:  %.4a\n", bench_result.max_rel_error);
            printf("  mean relative error: %.4a\n", bench_result.mean_rel_error);
            printf("  error norm 2:       %.4a\n", bench_result.error_norm2);
#           else
            // condensed display
            printf("%s, %d, %d, %.3e, %.3e, %.3e %.3e\n", 
                   benchmarks[benchId].label, n, bench_result.perf_count,
                   bench_result.max_abs_error, bench_result.max_rel_error, bench_result.error_norm2, bench_result.mean_rel_error);
#           endif
        }

        free(src);
        free(dst);
        free(golden);
    }


    return 0;
}
