#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <pthread.h>
#include <stdbool.h>

// [구조체 정의] (성능을 위해 노출)
typedef struct{
    void (*function)(void* arg);
    void* arg;
} Task;

typedef struct{
    Task* tasks;        // 원형 버퍼로 사용할 배열
    int capacity;       // 큐 최대 크기
    int size;           // 현재 들어있는 작업 수
    int head;           // pop 위치
    int tail;           // push 위치

    bool stop;           // 서버 종료 시 스레드를 깨우기 위한 플래그
    
    pthread_mutex_t mutex;          // 동기화
    pthread_cond_t cond_not_empty;  // 비어있지 않음을 알림 (pop 대기용)
    pthread_cond_t cond_not_full;   // 꽉 차있지 않음을 알림 (push 대기용)
} TaskQueue;



// [함수 선언]
// 초기화 및 해제
int task_queue_init(TaskQueue* q, int capacity);
void task_queue_shutdown(TaskQueue* q);
void task_queue_free(TaskQueue* q);

// 연산
void task_queue_enqueue(TaskQueue* q, Task task);  // 꽉 차면 대기 
/**
 * @brief 큐에 작업을 넣으려 시도함 (Non-blocking)
 * @return 성공시 0, 큐가 꽉 찼으면 -1
 */
int task_queue_try_enqueue(TaskQueue* q, Task task); 
Task task_queue_dequeue(TaskQueue *q);              // 비면 대기 
#endif

