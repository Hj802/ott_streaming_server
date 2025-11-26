#ifndef CLIENT_CONTEXT_H
#define CLIENT_CONTEXT_H

#include <sys/types.h>
#include <netinet/in.h>

typedef enum {
    STATE_REQ_RECEIVING,        // 요청 수신 중 (EPOLLIN 감시)
    STATE_PROCESSING,           // 워커 스레드 작업 중 (Epoll 감시 잠시 해제 or 무시)
    STATE_RES_SENDING_HEADER,   // 응답 헤더 전송 중 (EPOLLOUT 감시)
    STATE_RES_SENDING_BODY,     // 파일 바디 전송 중 (EPOLLOUT 감시)
    STATE_CLOSED                // 종료 대기
} ClientState;

typedef struct {
    int client_fd;                      // 클라이언트 소켓
    char client_ip[INET_ADDRSTRLEN];    // 클라이언트 ip
    time_t last_active;                 // Resource Leak 방지

    char buffer[4096];  // 송수신 버퍼 (재사용)
    int buffer_len;     // 버퍼에 담긴 유효 데이터 크기
    int buffer_sent;    // 현재까지 보낸 바이트 수

    ClientState state;

    int file_fd;            
    off_t file_offset;  // 현재 파일 위치

    size_t bytes_remaining; // 남은 파일 크기 (Chunk)
} ClientContext;

#endif