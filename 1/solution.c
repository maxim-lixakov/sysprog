#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include "merge_sort.h"

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

struct my_context {
	char *name;
	/** ADD HERE YOUR OWN MEMBERS, SUCH AS FILE NAME, WORK TIME, ... */
};

static struct my_context *
my_context_new(const char *name)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}

/**
 * A function, called from inside of coroutines recursively. Just to demonstrate
 * the example. You can split your code into multiple functions, that usually
 * helps to keep the individual code blocks simple.
 */
static void
other_function(const char *name, int depth)
{
	printf("%s: entered function, depth = %d\n", name, depth);
	coro_yield();
	if (depth < 3)
		other_function(name, depth + 1);
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *context)
{
	/* IMPLEMENT SORTING OF INDIVIDUAL FILES HERE. */

	struct coro *this = coro_this();
	struct my_context *ctx = context;
	char *name = ctx->name;
	printf("Started coroutine %s\n", name);
	printf("%s: switch count %lld\n", name, coro_switch_count(this));
	printf("%s: yield\n", name);
	coro_yield();

    FILE *input_file = fopen(name, "r");
    if (!input_file){
        perror("Error opening input file");
        my_context_delete(ctx);
        return 1;
    }

    int *array = NULL;
    int size = 0;
    int number;

    while (fscanf(input_file, "%d", &number) == 1){
        array = realloc(array, (size + 1) * sizeof(int));
        if (array == NULL) {
            perror("Error reallocating memory");
            return 1;
        }
        array[size++] = number;
    }

    mergeSort(array, 0, size -1);

    fclose(input_file);

    FILE *temp_output_file = fopen("temp_sorted_file.txt", "w");

    if (!temp_output_file){
        perror("Error opening temporary output file");
        free(array);
        my_context_delete(ctx);
        return 1;
    }

    for (int i = 0; i < size; ++i) {
        fprintf(temp_output_file, "%d ", array[i]);
    }

    fclose(temp_output_file);

    // replace the original input file with the temporary output file
    if (rename("temp_sorted_file.txt", name) != 0) {
        perror("Error renaming temporary file");
        free(array);
        my_context_delete(ctx);
        return 1;
    }

	printf("%s: switch count %lld\n", name, coro_switch_count(this));
	printf("%s: yield\n", name);
	coro_yield();

	printf("%s: switch count %lld\n", name, coro_switch_count(this));
	other_function(name, 1);
	printf("%s: switch count after other function %lld\n", name,
	       coro_switch_count(this));

	my_context_delete(ctx);
	/* This will be returned from coro_status(). */
	return 0;
}

int
main(int argc, char **argv)
{
	int num_files = argc - 1;

	/* Initialize our coroutine global cooperative scheduler. */
	coro_sched_init();

	/* Start several coroutines. */
    for (int i = 1; i <= num_files; ++i) {
        struct my_context *ctx = my_context_new(argv[i]); // pass the file name to the context
        coro_new(coroutine_func_f, ctx);
    }

	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		printf("Finished %d\n", coro_status(c));
		coro_delete(c);
	}
	/* All coroutines have finished. */

    int* merged_array = NULL;
    int number;
    int size = 0;

    for (int i = 1; i <= num_files; ++i) {
        char *name = argv[i];
        FILE *temp_sorted_file = fopen(name, "r");
        if (temp_sorted_file == NULL) {
            perror("Error opening file");
            return 1;
        }
        while (fscanf(temp_sorted_file, "%d", &number) == 1){
            merged_array = realloc(merged_array, (size + 1)*sizeof(int));
            merged_array[size++] = number;
            if (merged_array == NULL) {
                perror("Error reallocating memory");
                return 1; // Return an error code on memory allocation failure
            }
        }
        fclose(temp_sorted_file);
    }

    mergeSort(merged_array, 0, size-1);

    FILE *output_file = fopen("result.txt", "w");
    if (output_file == NULL) {
        perror("Error opening output file");
        return 1;
    }
    for (int i = 0; i < size; ++i) {
        fprintf(output_file, "%d ", merged_array[i]);
    }

    free(merged_array);
    fclose(output_file);

	return 0;
}
