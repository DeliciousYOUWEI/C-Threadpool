#ifndef __M_THREADPOOL__
#define __M_THREADPOOL__

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

typedef struct taskcallback{
	void* (*callback)(void*);
	void* args;
}taskcallback;

typedef struct loop_task_queue{
	pthread_mutex_t queue_locker;
	pthread_cond_t queue_not_empty;
	pthread_cond_t queue_not_full;
	
	taskcallback* taskqueue;
	int queue_max_len;
	int queue_cur_len;
	int queue_head;
	int queue_end;  //end is the place where next task need to go
}loop_task_queue;

typedef struct tasklinkinfo{
	pthread_mutex_t link_info_locker;
	pthread_cond_t task_linked;  // when a task was linked to a thread, trigger this signal.
	pthread_cond_t info_already_read;
	int changable;

	void* (*callback)(void*);
	pthread_t linked_tid;
}tasklinkinfo;

typedef struct threadpool threadpool;
struct threadpool{
	// Basic information
	pthread_mutex_t data_locker;

	tasklinkinfo linkinfo;

	int pool_run;  // this controller should out of locker

	int max_size;
	int min_size;
	int thread_count;
	int busy_thread_count;
	int shrink_thread_count;  // threads need to be shrink

	loop_task_queue taskqueue;
	};

threadpool* threadpool_create(int max_size, int min_size);

void threadpool_submit(threadpool* pool, void*(*start_route)(void*), void* args);

void* threadpool_wait(pthread_t tid);

int threadpool_stop(threadpool* pool);

int threadpool_destroy(threadpool* pool);

#endif
