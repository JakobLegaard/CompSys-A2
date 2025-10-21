#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fts.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "histogram.h"
#include "job_queue.h"

#ifndef UPDATE_INTERVAL_BYTES
#define UPDATE_INTERVAL_BYTES 100000
#endif

#ifndef WORKER_QUEUE_CAPACITY
#define WORKER_QUEUE_CAPACITY 256
#endif


static int g_hist[8] = {0};
static pthread_mutex_t g_hist_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct job_queue g_queue;


static void process_one_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }

    int local_hist[8] = {0};
    size_t bytes_since_merge = 0;
    int c;

    while ((c = fgetc(f)) != EOF) {
        unsigned char byte = (unsigned char)c;
        update_histogram(local_hist, byte);
        bytes_since_merge++;

        if (bytes_since_merge >= UPDATE_INTERVAL_BYTES) {
            pthread_mutex_lock(&g_hist_mutex);
            merge_histogram(local_hist, g_hist);
            print_histogram(g_hist);
            pthread_mutex_unlock(&g_hist_mutex);
            bytes_since_merge = 0;
        }
    }

    pthread_mutex_lock(&g_hist_mutex);
    merge_histogram(local_hist, g_hist);
    print_histogram(g_hist);
    pthread_mutex_unlock(&g_hist_mutex);

    fclose(f);
}

static void *worker_main(void *arg) {
    (void)arg;
    char *path = NULL;

    while (job_queue_pop(&g_queue, (void**)&path) == 0) {
        if (path) {
            process_one_file(path);
            free(path);
            path = NULL;
        }
    }

    return NULL;
}


static int enqueue_files(char **roots, int nroots) {
    char **paths = calloc((size_t)nroots + 1, sizeof(char *));
    if (!paths) return -1;
    for (int i = 0; i < nroots; i++) paths[i] = roots[i];
    paths[nroots] = NULL;

    FTS *fts = fts_open(paths, FTS_NOCHDIR | FTS_PHYSICAL, NULL);
    if (!fts) {
        perror("fts_open");
        free(paths);
        return -1;
    }

    FTSENT *ent;
    while ((ent = fts_read(fts)) != NULL) {
        if (ent->fts_info == FTS_F) {
            char *dup = strdup(ent->fts_path);
            if (!dup) continue;
            job_queue_push(&g_queue, dup);
        }
    }

    fts_close(fts);
    free(paths);
    return 0;
}


static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-n threads] DIR [DIR2 ...]\n", prog);
    exit(1);
}

int main(int argc, char **argv) {
    int nthreads = 4;
    int i = 1;

    if (i + 1 < argc && strcmp(argv[i], "-n") == 0) {
        char *end = NULL;
        long v = strtol(argv[i + 1], &end, 10);
        if (end == argv[i + 1] || v <= 0) {
            fprintf(stderr, "Invalid thread count: %s\n", argv[i + 1]);
            return 1;
        }
        nthreads = (int)v;
        i += 2;
    }

    if (i >= argc) usage(argv[0]);

    char **roots = &argv[i];
    int nroots = argc - i;

    if (job_queue_init(&g_queue, WORKER_QUEUE_CAPACITY) != 0) {
        fprintf(stderr, "job_queue_init failed\n");
        return 1;
    }

    pthread_t *threads = calloc((size_t)nthreads, sizeof(pthread_t));
    if (!threads) {
        fprintf(stderr, "thread alloc failed\n");
        job_queue_destroy(&g_queue);
        return 1;
    }

    for (int t = 0; t < nthreads; t++) {
        pthread_create(&threads[t], NULL, worker_main, NULL);
    }

    enqueue_files(roots, nroots);

    job_queue_destroy(&g_queue);

    for (int t = 0; t < nthreads; t++) {
        pthread_join(threads[t], NULL);
    }

    pthread_mutex_lock(&g_hist_mutex);
    print_histogram(g_hist);
    pthread_mutex_unlock(&g_hist_mutex);

    free(threads);
    pthread_mutex_destroy(&g_hist_mutex);
    return 0;
}
