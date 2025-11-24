#ifndef REACTOR_H
#define REACTOR_H

#include <stdbool.h>

typedef struct Threadpool ThreadPool;

typedef struct {
    int epoll_fd;       // epoll 인스턴스 파일 디스크립터
    int listen_fd;      // 서버의 리스닝 소켓 (문지기)
    int port;           // 서버 포트 번호
    
    ThreadPool* pool;   // 작업을 이관할 스레드 풀
    
    volatile bool running; // 이벤트 루프 종료 제어용 플래그
} Reactor;


/**
 * @brief Reactor 초기화
 * 소켓 생성, 바인드, 리슨, epoll 생성을 수행함.
 * * @param reactor 초기화할 구조체 포인터
 * @param port 서버 포트 (e.g., 8080)
 * @param pool 작업을 넘길 스레드 풀 포인터
 * @return 성공 0, 실패 -1
 */
int reactor_init(Reactor* reactor, int port, ThreadPool* pool);

/**
 * @brief 이벤트 루프 실행 (Blocking)
 * 무한 루프를 돌며 epoll_wait를 호출함.
 * - 접속 요청(Accept) -> 즉시 처리
 * - 데이터 수신(Recv) -> 파싱 후 스레드 풀에 Submit
 * * @param reactor 구조체 포인터
 */
void reactor_run(Reactor* reactor);

/**
 * @brief Reactor 종료 요청
 * 실행 중인 루프를 멈추게 함.
 */
void reactor_stop(Reactor* reactor);

/**
 * @brief 자원 해제
 * 소켓과 epoll fd를 닫음.
 */
void reactor_destroy(Reactor* reactor);

#endif // REACTOR_H