#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include <time.h>

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

struct my_context {
    char *name;
    struct timespec start_time;
    struct timespec end_time;
    struct timespec active_start_time; // Время начала активной работы
    long active_work_time; // Активное время работы
    int switch_count; // Счетчик переключений корутины
    struct timespec idle_start_time; // Время начала простоя
    long total_idle_time; // Общее время простоя
};

static void mergeSort(int *array, int left, int right, struct my_context *ctx);
static void merge(int *array, int left, int middle, int right, struct my_context *ctx);
static struct my_context *my_context_new(const char *name);
static void my_context_delete(struct my_context *ctx);

static struct my_context *my_context_new(const char *name) {
    struct my_context *ctx = malloc(sizeof(*ctx));
    ctx->name = strdup(name);
    ctx->switch_count = 0;
    ctx->active_work_time = 0; // Инициализация активного времени работы
    ctx->total_idle_time = 0; // Инициализация общего времени простоя
    clock_gettime(CLOCK_MONOTONIC, &ctx->active_start_time); // Запоминаем время начала активной работы
    return ctx;
}

static void my_context_delete(struct my_context *ctx) {
    free(ctx->name);
    free(ctx);
}


void merge(int arr[], int l, int m, int r, struct my_context *ctx) {
    int i, j, k;
    int n1 = m - l + 1;
    int n2 = r - m;

    int *L = malloc(n1 * sizeof(int));
    int *R = malloc(n2 * sizeof(int));

    for (i = 0; i < n1; i++)
        L[i] = arr[l + i];
    for (j = 0; j < n2; j++)
        R[j] = arr[m + 1 + j];

    i = 0;
    j = 0;
    k = l;
    while (i < n1 && j < n2) {

        clock_gettime(CLOCK_MONOTONIC, &ctx->idle_start_time);
        coro_yield();
        struct timespec idle_end_time;
        clock_gettime(CLOCK_MONOTONIC, &idle_end_time);

        long idle_time = (idle_end_time.tv_sec - ctx->idle_start_time.tv_sec) * 1000000 + (idle_end_time.tv_nsec - ctx->idle_start_time.tv_nsec) / 1000;
        ctx->total_idle_time += idle_time;
        ctx->switch_count++; // Увеличение счетчика переключений
        if (L[i] <= R[j]) {
            arr[k] = L[i++];
        } else {
            arr[k] = R[j++];
        }
        k++;
    }

    while (i < n1) {
        coro_yield(); // Переключение корутины
        ctx->switch_count++; // Увеличение счетчика переключений
        arr[k++] = L[i++];
    }

    while (j < n2) {
        coro_yield(); // Переключение корутины
        ctx->switch_count++; // Увеличение счетчика переключений
        arr[k++] = R[j++];
    }

    free(L);
    free(R);
}

void mergeSort(int arr[], int l, int r, struct my_context *ctx) {
    if (l < r) {
        int m = l + (r - l) / 2;
        mergeSort(arr, l, m, ctx);
        mergeSort(arr, m + 1, r, ctx);
        merge(arr, l, m, r, ctx);
    }
}

/**
 * A function, called from inside of coroutines recursively. Just to demonstrate
 * the example. You can split your code into multiple functions, that usually
 * helps to keep the individual code blocks simple.
 */
//static void
//other_function(const char *name, int depth)
//{
//	printf("%s: entered function, depth = %d\n", name, depth);
//	coro_yield();
//	if (depth < 3)
//		other_function(name, depth + 1);
//}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int coroutine_func_f(void *context) {
    struct my_context *ctx = context;
    char *name = ctx->name;

    clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
    printf("Started coroutine %s\n", name);

    // Open file and read numbers
    FILE *input_file = fopen(name, "r");
    if (!input_file) {
        perror("Error opening input file");
        my_context_delete(ctx);
        return 1;
    }

    int *array = NULL;
    int size = 0, number;
    while (fscanf(input_file, "%d", &number) == 1) {
        array = realloc(array, (size + 1) * sizeof(int));
        if (!array) {
            perror("Error reallocating memory");
            fclose(input_file);
            my_context_delete(ctx);
            return 1;
        }
        array[size++] = number;
    }
    fclose(input_file);

    // Sort the array
    mergeSort(array, 0, size - 1, ctx);

    // Save the sorted array to the same file
    FILE *output_file = fopen(name, "w");
    if (!output_file) {
        perror("Error opening output file");
        free(array);
        my_context_delete(ctx);
        return 1;
    }
    for (int i = 0; i < size; ++i) {
        fprintf(output_file, "%d ", array[i]);
    }
    fclose(output_file);

    free(array);

    clock_gettime(CLOCK_MONOTONIC, &ctx->end_time);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long active_time = (now.tv_sec - ctx->active_start_time.tv_sec) * 1000000 + (now.tv_nsec - ctx->active_start_time.tv_nsec) / 1000 - ctx->total_idle_time;
    ctx->active_work_time += active_time; // Добавляем к общему активному времени работы

    printf("%s: Active execution time: %.3f seconds\n", ctx->name, (double)ctx->active_work_time / 1000000);
    printf("Coroutine %s switch count: %d\n", ctx->name, ctx->switch_count);
    my_context_delete(ctx);
    return 0;
}

int main(int argc, char **argv) {
    struct timespec program_start, program_end;
    clock_gettime(CLOCK_MONOTONIC, &program_start);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file1> [<file2> ...]\n", argv[0]);
        return 1;
    }

    int num_files = argc - 1;

    // Initialize the coroutine global cooperative scheduler.
    coro_sched_init();

    // Start coroutines for each valid file argument.
    for (int i = 1; i <= num_files; ++i) {
        if (argv[i][0] == '-') {
            printf("Skipping invalid argument: %s\n", argv[i]);
            continue; // Skip arguments starting with '-'
        }
        struct my_context *ctx = my_context_new(argv[i]); // Pass the file name to the context.
        if (!ctx) {
            fprintf(stderr, "Failed to create context for: %s\n", argv[i]);
            continue;
        }
        coro_new(coroutine_func_f, ctx);
    }

    // Wait for all the coroutines to end.
    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        printf("Finished %d\n", coro_status(c));
        coro_delete(c);
    }

    clock_gettime(CLOCK_MONOTONIC, &program_end);
    long total_program_time = (program_end.tv_sec - program_start.tv_sec) * 1000000 +
                              (program_end.tv_nsec - program_start.tv_nsec) / 1000;
    printf("Total program execution time: %.3f seconds\n", (double)total_program_time / 1000000);

    return 0;
}