#include "mine-threadpool.h"
#include <unistd.h>

void* callback(void* args){
	printf("callback begin to process in %lu\n", pthread_self());
	sleep(2);
	printf("callback over in %lu over\n", pthread_self());
}

int main(int argc, char*args){
	threadpool* pool = threadpool_create(100, 5);
	printf("create success, main thread: %lu\n", pthread_self());
	
	printf("begin submit\n");
	int looper;
	for (looper = 0; looper < 5; looper++){
		threadpool_submit(pool, callback, NULL);
	}
	
	sleep(5);
	printf("main thread over\n");

}
