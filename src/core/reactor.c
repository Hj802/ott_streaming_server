/*
#역할: Event Loop 그 자체. epoll의 Wrapper

# 핵심 기능: 
- reactor_create(): epoll_create1 호출.
- reactor_add(fd, callback): 감시할 소켓(fd)과, 이벤트 발생 시 실행할 함수(callback)를 등록.
- reactor_run(): epoll_wait를 무한 루프(while(1))로 돌면서, 이벤트가 터지면 등록된 callback 함수를 실행.
- 시니어의 팁: 모든 소켓은 Non-blocking 모드로 설정되어야 하네. (fcntl 사용)

- 한순간도 blocking 되면 X
*/
#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "core/reactor.h"
#include "core/thread_pool.h"
#include "core/config_loader.h"

#define MAX_EVENTS 1024

static int set_nonblocking(int socket_fd);

int reactor_init(Reactor *reactor, ThreadPool *pool, const ServerConfig *config){
    if (!reactor || !pool || !config) return -1;

    // 변수 초기화
    reactor->pool = pool;
    reactor->running = false;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", config->port);
    
    // 주소 설정
    printf("Configuring local address...\n");
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *result_list;
    int ret = getaddrinfo(config->server_host, port_str, &hints, &result_list);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo() failed: %s\n", gai_strerror(ret));
        return -1;
    }

    // 바인딩 가능한 주소를 찾을 때까지 순회
    struct addrinfo *rp;
    int listen_socket = -1;

    for (rp = result_list; rp != NULL; rp = rp->ai_next) {
        listen_socket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_socket < 0) continue; // 실패하면 다음 주소 시도

        // 옵션 설정
        int opt = 1;
        if(setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
            perror("setsockopt() failed");
            close(listen_socket);
            listen_socket = -1;
            continue;
        }

        if (bind(listen_socket, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; // 바인딩 성공. 루프 탈출
        }

        close(listen_socket); // 바인드 실패 시 닫고 다음으로
        listen_socket = -1;
    }
    
    freeaddrinfo(result_list);

    if (listen_socket < 0) {
        perror("Failed to bind to any address");
        return -1;
    }

    reactor->listen_fd = listen_socket; // 성공한 소켓 저장

    // non-blocking 설정
    if(set_nonblocking(reactor->listen_fd) < 0){
        perror("set_nonblocking() failed");
        close(reactor->listen_fd);
        return -1;
    }

    // 연결 대기
    printf("Listening...\n");
    if (listen(reactor->listen_fd, config->max_clients)){
        perror("listen() failed");
        close(reactor->listen_fd);
        return -1;
    }

    // epoll 생성
    reactor->epoll_fd = epoll_create1(0);
    if (reactor->epoll_fd < 0){
        perror("epoll_create1 failed.");
        close(reactor->listen_fd);
        return -1;
    }

    // 리스너 등록
    struct epoll_event event = {0};
    event.data.fd = reactor->listen_fd;
    event.events = EPOLLIN;
    if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, reactor->listen_fd, &event) < 0){
        perror("epoll_ctl() failed");
        close(reactor->listen_fd);
        close(reactor->epoll_fd);
        return -1;
    }
    return 0;
}

static int set_nonblocking(int socket_fd){
    int flag = fcntl(socket_fd, F_GETFL, 0);
    if (flag == -1) return -1;
    if (fcntl(socket_fd, F_SETFL, flag | O_NONBLOCK) == -1) return -1;
    return 0;
}

void reactor_run(Reactor* reactor){

}


void reactor_stop(Reactor* reactor){

}


void reactor_destroy(Reactor* reactor){
    
}