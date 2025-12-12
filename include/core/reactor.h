#ifndef REACTOR_H
#define REACTOR_H

#include <stdbool.h>

typedef struct ThreadPool ThreadPool;
typedef struct ServerConfig ServerConfig;

typedef struct {
    int epoll_fd;       // epoll 인스턴스 파일 디스크립터
    int listen_fd;      // 서버의 리스닝 소켓
    
    ThreadPool* pool;   // 작업을 이관할 스레드 풀
    
    volatile bool running; // 이벤트 루프 종료 제어용 플래그
} Reactor;

/**
 * @brief Reactor 초기화
 * 소켓 생성, 바인드, 리슨, epoll 생성을 수행함.
 * @param reactor 초기화할 구조체 포인터
 * @param pool 작업을 넘길 스레드 풀 포인터
 * @param config 상수값을 받아올 포인터
 * @return 성공 0, 실패 -1
 */
int reactor_init(Reactor *reactor, ThreadPool *pool, const ServerConfig *config);

/**
 * @brief 이벤트 루프 실행 (Control Flow Blocking)
 * 무한 루프를 돌며 epoll_wait를 호출함.
 * - 접속 요청(Accept) -> 즉시 처리
 * - 데이터 수신(Recv) -> 파싱 후 스레드 풀에 Submit
 * * @param reactor 구조체 포인터
 */
void reactor_run(Reactor *reactor);

/**
 * @brief Reactor 종료 요청
 * 실행 중인 루프를 멈추게 함.
 */
void reactor_stop(Reactor *reactor);

/**
 * @brief 자원 해제
 * 소켓과 epoll fd를 닫음.
 */
void reactor_destroy(Reactor *reactor);

/**
 * @brief 감시 중인 FD의 이벤트를 변경합니다. (EPOLL_CTL_MOD 래퍼)
 * * @param epoll_fd  Reactor의 epoll 인스턴스
 * @param target_fd 감시 대상 파일 디스크립터 (Client Socket)
 * @param events    감시할 이벤트 (EPOLLIN, EPOLLOUT 등)
 * @param context   이벤트 발생 시 돌려받을 컨텍스트 포인터 (User Data)
 * @return 성공 시 0, 실패 시 -1
 */
int reactor_update_event(int epoll_fd, int target_fd, int events, void *context);

#endif