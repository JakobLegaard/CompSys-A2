// Setting _DEFAULT_SOURCE is necessary to activate visibility of
// certain header file contents on GNU/Linux systems.
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>

// err.h contains various nonstandard BSD extensions, but they are
// very handy.
#include <err.h>

#include <pthread.h>

#include "job_queue.h"

pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

struct worker_args {
    struct job_queue *jq;
    const char *needle;
};

void* worker(void *arg) {
    struct worker_args *args = (struct worker_args*)arg;
    
    while (1) {
        char *path;
        if (job_queue_pop(args->jq, (void**)&path) != 0) {
            break;
        }
        FILE *f = fopen(path, "r");
        if (f == NULL) {
            pthread_mutex_lock(&stdout_mutex);
            warn("failed to open %s", path);
            pthread_mutex_unlock(&stdout_mutex);
            free(path);
            continue;
        }
        
        char *line = NULL;
        size_t linelen = 0;
        int lineno = 1;
        
        while (getline(&line, &linelen, f) != -1) {
            if (strstr(line, args->needle) != NULL) {
                pthread_mutex_lock(&stdout_mutex);
                printf("%s:%d: %s", path, lineno, line);
                pthread_mutex_unlock(&stdout_mutex);
            }
            lineno++;
        }
        
        free(line);
        fclose(f);
        free(path);
    }
    
    return NULL;
}

int main(int argc, char * const *argv) {
  if (argc < 2) {
    err(1, "usage: [-n INT] STRING paths...");
    exit(1);
  }

  int num_threads = 1;
  char const *needle = argv[1];
  char * const *paths = &argv[2];


  if (argc > 3 && strcmp(argv[1], "-n") == 0) {
    num_threads = atoi(argv[2]);

    if (num_threads < 1) {
      err(1, "invalid thread count: %s", argv[2]);
    }

    needle = argv[3];
    paths = &argv[4];

  } else {
    needle = argv[1];
    paths = &argv[2];
  }

  struct job_queue jq;
  if (job_queue_init(&jq, 64) != 0) {
    err(1, "job_queue_init() failed");
  }

  struct worker_args args = {
    .jq = &jq,
    .needle = needle
  };

  pthread_t *threads = calloc(num_threads, sizeof(pthread_t));
  if (threads == NULL) {
    err(1, "calloc() failed");
  }

  for (int i = 0; i < num_threads; i++) {
    if (pthread_create(&threads[i], NULL, &worker, &args) != 0) {
      err(1, "pthread_create() failed");
    }
  }

  int fts_options = FTS_LOGICAL | FTS_NOCHDIR;

  FTS *ftsp;
  if ((ftsp = fts_open(paths, fts_options, NULL)) == NULL) {
    err(1, "fts_open() failed");
    return -1;
  }

  FTSENT *p;
  while ((p = fts_read(ftsp)) != NULL) {
    switch (p->fts_info) {
    case FTS_D:
      break;
    case FTS_F:
      if (job_queue_push(&jq, strdup(p->fts_path)) != 0) {
        err(1, "job_queue_push() failed");
      }
      break;
    default:
      break;
    }
  }

  fts_close(ftsp);

  job_queue_destroy(&jq);

  for (int i = 0; i < num_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      err(1, "pthread_join() failed");
    }
  }

  free(threads);

  return 0;
}
