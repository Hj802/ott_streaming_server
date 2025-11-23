#define _POSIX_C_SOURCE 200112L
/*
# 역할: 메인 스레드(Reactor)와 워커 스레드 사이의 '우체통'.

# 핵심 기능:
- 자료구조: 연결 리스트(Linked List)나 원형 버퍼로 구현된 큐.
- enqueue(task): 메인 스레드가 작업을 넣음. (반드시 Mutex 락 필요!)
- dequeue(): 워커 스레드가 작업을 꺼냄. (반드시 Mutex 락 필요!)
- 주의: 여기가 **동시성 문제(Race Condition)**가 발생하는 지점. 
pthread_mutex_lock과 pthread_cond_signal을 정확히 사용해야 하네.
*/
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
}// task_queue_init()

void task_queue_destroy(TaskQueue* q){

}

// push, pop
void task_queue_push(TaskQueue* q, Task task){

}
Task task_queue_pop(TaskQueue *q){

}

// 상태 확인 (참고용! 로직 제어용 X)
bool task_queue_is_empty(TaskQueue* q){
    
}
bool task_queue_is_full(TaskQueue* q){

}