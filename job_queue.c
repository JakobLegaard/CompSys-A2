#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "job_queue.h"
#include <pthread.h>


int job_queue_init(struct job_queue *job_queue, int capacity) {
  job_queue -> capacity = capacity;
  job_queue -> front = 0;
  job_queue -> back = -1;
  job_queue -> num_jobs = 0;
  job_queue -> destroying = 0;
  job_queue -> data = malloc(capacity * sizeof(void *));
  if (job_queue -> data == NULL) {
    return -1; 
  }
  pthread_mutex_init(&job_queue -> mutex, NULL);
  pthread_cond_init(&job_queue -> nonempty, NULL);
  pthread_cond_init(&job_queue -> not_full_yet, NULL);
  pthread_cond_init(&job_queue -> done, NULL);
  return 0;
}

int job_queue_destroy(struct job_queue *job_queue) {
  pthread_mutex_lock(&job_queue -> mutex);
  job_queue -> destroying = 1;

  pthread_cond_broadcast(&job_queue -> nonempty);
  pthread_cond_broadcast(&job_queue -> not_full_yet);

  while (job_queue -> num_jobs > 0) {
    pthread_cond_wait(&job_queue -> done, &job_queue -> mutex);
  }
  pthread_mutex_unlock(&job_queue -> mutex);

  pthread_mutex_destroy(&job_queue -> mutex);
  pthread_cond_destroy(&job_queue -> nonempty);
  pthread_cond_destroy(&job_queue -> not_full_yet);
  pthread_cond_destroy(&job_queue -> done);

  free(job_queue -> data);

  return 0;
}

int job_queue_push(struct job_queue *job_queue, void *data) {
  pthread_mutex_lock(&job_queue -> mutex);

  while (job_queue -> num_jobs == job_queue -> capacity) {
    if (job_queue -> destroying) {
      pthread_mutex_unlock(&job_queue -> mutex);
      return -1;
    }
    pthread_cond_wait(&job_queue -> not_full_yet, &job_queue -> mutex);
  }
    job_queue -> back = (job_queue -> back + 1) % job_queue -> capacity;
    job_queue -> data[job_queue -> back] = data;
    job_queue -> num_jobs++;
    pthread_cond_signal(&job_queue -> nonempty);
    pthread_mutex_unlock(&job_queue -> mutex);
    return 0;
  }

int job_queue_pop(struct job_queue *job_queue, void **data) {
  pthread_mutex_lock(&job_queue -> mutex);

  while (job_queue -> num_jobs == 0){
    if (job_queue -> destroying) {
      pthread_mutex_unlock(&job_queue -> mutex);
      return -1;
    }
    pthread_cond_wait(&job_queue -> nonempty, &job_queue -> mutex);
  }
  *data = job_queue -> data[job_queue -> front];
  job_queue -> front = (job_queue -> front + 1) % job_queue -> capacity;
  job_queue -> num_jobs--;

  if (job_queue -> num_jobs == 0 && job_queue -> destroying) {
    pthread_cond_signal(&job_queue -> done);
}
pthread_cond_signal(&job_queue -> not_full_yet);
pthread_mutex_unlock(&job_queue -> mutex);
return 0;
}