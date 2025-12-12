#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include "core/task_queue.h"
#include <pthread.h>

// 구조체 정의
typedef struct ThreadPool{
    TaskQueue queue;        // 임베딩 구조체
    pthread_t* threads;     // 일꾼 스레드들의 ID 배열 (동적 할당 예정)
    int num_threads;        // 스레드 개수
} ThreadPool;


// 생명 주기 함수
/**
 * @brief 스레드 풀 초기화
 * @param pool 풀 구조체 포인터
 * @param num_threads 생성할 워커 스레드 수
 * @param queue_capacity 작업 큐의 최대 크기
 * @return 성공 0, 실패 -1
 */
int thread_pool_init(ThreadPool* pool, int num_threads, int queue_capacity);

/**
 * @brief 작업을 풀에 제출 (편의 함수)
 * 내부적으로 Task 구조체를 생성하여 큐에 enqueue 함.
 * @param pool 풀 포인터
 * @param function 실행할 함수 포인터
 * @param arg 함수에 전달할 인자
 */
int thread_pool_submit(ThreadPool* pool, void (*function)(void*), void* arg);

/**
 * @brief 종료 1단계: 폐점 선언 (Non-blocking)
 * 더 이상 작업을 받지 않고, 대기 중인 스레드를 깨움.
 */
void thread_pool_shutdown(ThreadPool* pool);

/**
 * @brief 종료 2단계: 퇴근 대기 (Blocking)
 * 모든 워커 스레드가 종료될 때까지 메인 스레드가 기다림 (pthread_join).
 */
void thread_pool_wait(ThreadPool* pool);

/**
 * @brief 종료 3단계: 자원 해제
 * 할당된 메모리(threads 배열, 큐 내부 등)를 해제함.
 */
void thread_pool_cleanup(ThreadPool* pool);

#endif