#include "mine-threadpool.h"
#include <time.h>
#include <string.h>
#include <unistd.h>

int link_info_init(tasklinkinfo* info){
	int ret = pthread_mutex_init(&info->link_info_locker, NULL);
	if (ret != 0){
		fprintf(stderr, "locker init error: %s\n", strerror(ret));
		return -1;
	}

	ret = pthread_cond_init(&info->task_linked, NULL);
	if (ret != 0){
		fprintf(stderr, "condition init error: %s\n", strerror(ret));
		return -1;
	}

	ret = pthread_cond_init(&info->info_already_read, NULL);
	if (ret != 0){
		fprintf(stderr, "condition init error: %s\n", strerror(ret));
		return -1;
	}

	info->changable = 1;

	return 0;
}

// loop_queue operate
int task_queue_init(loop_task_queue* queue, int max_size){
	int ret = pthread_mutex_init(&queue->queue_locker, NULL);
	if (ret != 0){
		fprintf(stderr, "locker init error: %s\n", strerror(ret));
		return -1;
	}
	
	ret = pthread_cond_init(&queue->queue_not_empty, NULL);
	if (ret != 0){
		fprintf(stderr, "condition init error: %s\n", strerror(ret));
		return -1;
	}

	ret = pthread_cond_init(&queue->queue_not_full, NULL);
	if (ret != 0){
		fprintf(stderr, "condition init error: %s\n", strerror(ret));
		return -1;
	}

	queue->queue_max_len = max_size;
	queue->queue_cur_len = 0;
	queue->queue_head = 0;
	queue->queue_end = 0;
	queue->taskqueue = (taskcallback*)malloc(sizeof(taskcallback)*max_size);
	if (queue->taskqueue == NULL){
		fprintf(stderr, "queue malloc error\n");
		return -1;  // Destruction will be perform outside, because struct in pool isn't pointer but entity, we will free hole struct pool when get error.
	}
	return 0;
}

int task_queue_add(loop_task_queue* queue, void*(*start_routine)(void*), void* args){
	pthread_mutex_lock(&(queue->queue_locker));

	if (queue->queue_max_len == queue->queue_cur_len){
		struct timespec time_to_wait;
		int ret = clock_gettime(CLOCK_MONOTONIC, &time_to_wait);
		if (ret != 0) return -1;
		time_to_wait.tv_sec = time_to_wait.tv_sec+3;
		if (pthread_cond_timedwait(&(queue->queue_not_full), &(queue->queue_locker), &time_to_wait) == ETIMEDOUT) return -1;
	}

	queue->taskqueue[queue->queue_end].callback = start_routine;
	queue->queue_end = (queue->queue_end+1)%(queue->queue_max_len);
	queue->taskqueue[queue->queue_end].args = args;
	queue->queue_cur_len++;
	printf("task submitted\n");
	pthread_mutex_unlock(&(queue->queue_locker));

	pthread_cond_signal(&(queue->queue_not_empty));  // TODO:urgent need for exam:should be inside the locker?
	return 0;  // TODO:<how to declear every err? by using enum?>
}

int task_queue_pop(loop_task_queue* queue, taskcallback* dst){
	// locker is already get
	
	if (queue->queue_cur_len == 0){
		queue->queue_head = 0;
		queue->queue_end = 0;
		
		struct timespec time_to_wait;
		int ret = clock_gettime(CLOCK_MONOTONIC, &time_to_wait);
		if (ret != 0) return -1;
		time_to_wait.tv_sec = time_to_wait.tv_sec+3;
		if (pthread_cond_timedwait(&(queue->queue_not_empty), &(queue->queue_locker), &time_to_wait) == ETIMEDOUT) return -1;
	}
	
	memcpy(dst, &(queue->taskqueue[queue->queue_head]), sizeof(taskcallback));
	queue->queue_head = (queue->queue_head+1)%(queue->queue_max_len);
	queue->queue_cur_len--;
	printf("task poped\n");
	pthread_mutex_unlock(&queue->queue_locker);

	pthread_cond_signal(&queue->queue_not_full);
	return 0;
}

// callbackshell is the waiter for each thread. because there isn't specific thread for a specific task, but any thread link to any task.
// besides, this shell also deal with convert between normal thread to busy thread.
void* callbackshell(void* args){
	
	threadpool* pool = (threadpool*)args;

	printf("new thread created\n");
	// assigning task
	taskcallback* assigned_task;
	assigned_task = (taskcallback*)malloc(sizeof(taskcallback));

	loop_task_queue* queue = &pool->taskqueue;
	while (1){
		pthread_mutex_lock(&queue->queue_locker);
		if (queue->queue_cur_len == 0){
			pthread_cond_wait(&queue->queue_not_empty, &queue->queue_locker);
		}

		// if success, code will continue, otherwise it will exit
		int ret = task_queue_pop(queue, assigned_task);
		// if fail, exit this thread	
		if (ret != 0){
			pthread_mutex_lock(&pool->data_locker);
			if (pool->shrink_thread_count > 0){
				printf("thread shrinked\n");
				pool->thread_count--;
				pool->shrink_thread_count--;
				pthread_mutex_unlock(&pool->data_locker);
				pthread_exit(NULL);
			}
			else{
				continue;  // deal with the group shock
			}
		}

		// set thread busy
		pthread_mutex_lock(&pool->data_locker);
		pool->busy_thread_count++;
		pthread_mutex_unlock(&pool->data_locker);
		
		// refresh link info =======================upgrade later========================
	//	tasklinkinfo* info = &pool->linkinfo;
	//	pthread_mutex_lock(&info->link_info_locker);
	//	if (info->changable != 1){
	//		pthread_cond_wait(&info->info_already_read, &info->link_info_locker);
	//	}
	//	info->callback = assigned_task->callback;
	//	info->tid = pthread_self();
	//	info->changable = 0;
	//	pthread_cond_broadcast(task_linked);
	//	pthread_mutex_unlock(&info->link_info_locker);
		
		// deal the callback
		assigned_task->callback(assigned_task->args);
	
		// set thread busy
		pthread_mutex_lock(&pool->data_locker);
		pool->busy_thread_count--;
		pthread_mutex_unlock(&pool->data_locker);

	}

}

// TODO:adjuster handle
void* adjuster_callback(void* args){
	printf("adjuster have been started\n");
	threadpool* pool = (threadpool*)args;
	int cur_max_size, cur_min_size, cur_thread_count, cur_busy_thread_count;

	while (pool->pool_run){
		pthread_mutex_lock(&pool->data_locker);
		cur_max_size = pool->max_size;
		cur_min_size = pool->min_size;
		cur_thread_count = pool->thread_count;
		cur_busy_thread_count = pool->busy_thread_count;
		pthread_mutex_unlock(&pool->data_locker);

		//TODO:deal--if need to shrink, shrink_thread_count should be set firstly
		sleep(10);
	}
}



// -------------------------------------------outer API-------------------------------------------

threadpool* threadpool_create(int max_size, int min_size){
	// parameter check
	if (max_size < min_size){
		fprintf(stderr, "parameter error\n");
		return NULL;
	}

	// information init
	threadpool* pool;
	pool = (threadpool*)malloc(sizeof(threadpool));
	if (pool == NULL){
		fprintf(stderr, "pool init error\n");
		return NULL;
	}
	pool->pool_run = 1;

	pool->max_size = max_size;
	pool->min_size = min_size > 0 ? min_size : 1;
	pool->thread_count = 0;
	pool->busy_thread_count = 0;
	pool->shrink_thread_count = 0;

	if (task_queue_init(&(pool->taskqueue), max_size) != 0){
		fprintf(stderr, "task queue init error\n");
		return NULL;
	}
	printf("queue init success\n");

	//if (link_info_init(&(pool->linkinfo))){
	//	fprintf(stderr, "link info init error\n");
	//	return NULL;
	//}

	// create min required threads and start their shell
	int looper, ret;
	pthread_t tid = pthread_self();
	for (looper = 0; looper < min_size; looper++){
		ret = pthread_create(&tid, NULL, callbackshell, (void*)pool);
		if (ret != 0){
			fprintf(stderr, "init threads created fail:%s, only %d thread was created\n", strerror(ret), looper);
			break;
		}
		pool->thread_count++;
		pthread_detach(tid);
	}

	sleep(1);  // let threads create for 1 sec

	// create and start adjuster
	pthread_t adjuster_id;
	adjuster_id = pthread_create(&adjuster_id, NULL, adjuster_callback, (void*)pool);  // TODO:is tids should be locked?

	return pool;
}


void threadpool_submit(threadpool* pool, void*(*start_route)(void*), void* args){
	// add task into the queue
	int ret = task_queue_add(&pool->taskqueue, start_route, args);
	if (ret != 0){
		fprintf(stderr, "threadpool error, maybe maxsize need to expand or something\n");
		return;
	}
//	pthread_t linked_tid;
//
//	// get the linked thread's tid for return
//	tasklinkinfo* info = &pool->linkinfo;
//	while (1){
//		pthread_mutex_lock(&info->link_info_locker);
//		pthread_cond_wait(&info->task_linked, &info->link_info_locker);
//		if (info->callback == start_route){
//			linked_tid = info->linked_tid;
//			info->changable = 1;
//			pthread_cond_signal(&info->info_already_read);
//			break;
//		}
//		pthread_mutex_unlock(&info->link_info_locker);
//	}
//	pthread_mutex_unlock(&info->link_info_locker);
//	return linked_tid;
}

int threadpool_destroy(threadpool* pool){
	
}
// TODO:check this func in python and code it
void* threadpool_wait(pthread_t tid){}

int threadpool_stop(threadpool* pool){}


