#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "core/thread_pool.h"

static void* worker_thread_func(void* arg); // 워커 스레드가 실행할 함수

typedef struct {
    ThreadPool* pool;
    int idx;
} WorkerArg;

int thread_pool_init(ThreadPool* pool, int num_threads, int queue_capacity){
    // 유효성 검사
    if (pool == NULL || num_threads <= 0 || queue_capacity <= 0) {
        return -1;
    }

    // 내부 TaskQueue 초기화
    if (task_queue_init(&pool->queue, queue_capacity) != 0) {
        return -1;
    }

    // 스레드 관리용 배열 할당
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    if (pool->threads == NULL) {
        task_queue_free(&pool->queue); // 큐 메모리 해제
        return -1;
    }

    pool->num_threads = num_threads;

    //  워커 스레드 생성 루프
    for (int i = 0; i < num_threads; ++i) {
        // arg로 pool 자체를 넘겨줍니다 (워커가 큐에 접근해야 하니까)
        
        WorkerArg* arg = (WorkerArg*)malloc(sizeof(WorkerArg));
        if(arg == NULL){
            perror("Failed to allocate memory for worker arg");
            // [중요] 롤백 로직 (All or Nothing)
            // 5번째에서 실패했다면, 0~3번 스레드는 이미 살아서 돌아가고 있음.
            // 얘네들을 안전하게 종료시켜야 함.

            // 큐 폐쇄 및 기상 신호 전송
            task_queue_shutdown(&pool->queue);

            // 이미 생성된 i개의 스레드들이 종료될 때까지 대기 (Join)
            for (int j = 0; j < i; ++j) {
                pthread_join(pool->threads[j], NULL);
            }

            // 자원 정리
            free(pool->threads);
            task_queue_free(&pool->queue);
            return -1;
        }
        arg->pool = pool;
        arg->idx = i;

        if (pthread_create(&pool->threads[i], NULL, worker_thread_func, arg) != 0) {
            perror("Failed to create worker thread");
            // 롤백 로직 (All or Nothing)
            task_queue_shutdown(&pool->queue);
            for (int j = 0; j < i; ++j) {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            task_queue_free(&pool->queue);
            return -1; // 실패 반환
        }// if
    }// for
    return 0; // 성공
}

static void* worker_thread_func(void* arg){
    WorkerArg* args = (WorkerArg*)arg;

    ThreadPool* pool = args->pool;
    int idx = args->idx;
    free(args);
    printf("Worker-%d started.\n", idx);
    while(true){
        Task task = task_queue_dequeue(&pool->queue);
        if(task.function == NULL && task.arg == NULL){
            printf("Worker thread %lu stopping.\n", (unsigned long)pthread_self());
            break;
        }
        task.function(task.arg);
    }
    return NULL;
}

int thread_pool_submit(ThreadPool* pool, void (*function)(void*), void* arg){
    if (pool == NULL || function == NULL) return -1;
    Task task = {.function = function, .arg = arg};
    return task_queue_try_enqueue(&pool->queue, task);
}

void thread_pool_shutdown(ThreadPool* pool){
    if (pool == NULL) return;
    task_queue_shutdown(&pool->queue);
}

void thread_pool_wait(ThreadPool* pool){
    if (pool == NULL || pool->threads == NULL) return;

    for (int i = 0; i < pool->num_threads; i++){
        pthread_join(pool->threads[i], NULL);
    }
}

void thread_pool_cleanup(ThreadPool* pool){
    if (pool == NULL) return;

    if (pool->threads != NULL) {
        free(pool->threads);
        pool->threads = NULL; // [안전] 댕글링 포인터 방지
    }

    // 2. 큐 내부 자원 해제
    task_queue_free(&pool->queue);
}