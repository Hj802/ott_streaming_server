#define _POSIX_C_SOURCE 200112L
#include "core/task_queue.h"
#include <stdio.h>
#include <stdlib.h>

int task_queue_init(TaskQueue* q, int capacity){
    // 메모리 할당 및 에러 처리
    q->tasks = (Task*)malloc(capacity * sizeof(Task));
    if(q->tasks == NULL){
        perror("Failed to allocate memory for task queue");
        return -1;
    }

    // 변수 초기화
    q->capacity = capacity;
    q->size = 0;
    q->head = 0;
    q->tail = 0;
    q->stop = false;

    // 동기화 객체 초기화
    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        perror("Mutex init failed");
        free(q->tasks); // 잊지 말 것
        return -1;
    }

    if (pthread_cond_init(&q->cond_not_empty, NULL) != 0) {
        perror("Cond not empty init failed");
        pthread_mutex_destroy(&q->mutex);
        free(q->tasks);
        return -1;
    }

    if (pthread_cond_init(&q->cond_not_full, NULL) != 0) {
        perror("Cond not full init failed");
        pthread_cond_destroy(&q->cond_not_empty);
        pthread_mutex_destroy(&q->mutex);
        free(q->tasks);
        return -1;
    }

    return 0;
}

// Producer
void task_queue_enqueue(TaskQueue* q, Task task){
    pthread_mutex_lock(&q->mutex);

    // 이미 종료 신호가 왔다면 더 이상 받지 않음
    if (q->stop) {
        pthread_mutex_unlock(&q->mutex);
        return; 
    }

    while (q->size == q->capacity) {
        // 빈 공간이 생길 때(not_full)까지 여기서 잠들기.
        // 잠들 때는 mutex를 잠깐 반납, 깨어나면 다시 잡기.
        pthread_cond_wait(&q->cond_not_full, &q->mutex);
    }

    q->tasks[q->tail] = task;
    q->tail = (q->tail + 1) % q->capacity;
    q->size++;

    pthread_cond_signal(&q->cond_not_empty);
    pthread_mutex_unlock(&q->mutex);
}

// Consumer
Task task_queue_dequeue(TaskQueue *q){
    pthread_mutex_lock(&q->mutex);
    while(q->size == 0 && !q->stop){
        pthread_cond_wait(&q->cond_not_empty, &q->mutex);
    }

    if (q->size == 0 && q->stop){ // stop 때문에 깸
        pthread_mutex_unlock(&q->mutex);
        
        // "독약(Poison Pill)" 또는 "빈 Task" 리턴
        // 워커 스레드는 function이 NULL인 것을 보고 루프를 종료함
        Task empty_task = { .function = NULL, .arg = NULL }; 
        return empty_task;
    }

    Task task = q->tasks[q->head];
    q->head = (q->head + 1) % q->capacity; 
    q->size--;

    pthread_cond_signal(&q->cond_not_full);
    pthread_mutex_unlock(&q->mutex);
    return task;
}

// 종료
void task_queue_destroy(TaskQueue* q){
    pthread_mutex_lock(&q->mutex);
    q->stop = true;
    pthread_cond_broadcast(&q->cond_not_empty);
    pthread_cond_broadcast(&q->cond_not_full);
    pthread_mutex_unlock(&q->mutex);
}