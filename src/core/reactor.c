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
#include "app/client_event_handler.h"
#include "app/client_context.h"

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

        if (bind(listen_socket, (struct sockaddr *)rp->ai_addr, rp->ai_addrlen) == 0) {
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
    event.events = EPOLLIN;     // Level Trigger
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
    struct epoll_event events[MAX_EVENTS];
    reactor->running = true;

    printf("Reactor loop started.\n");
    while(reactor->running){
        int n_events = epoll_wait(reactor->epoll_fd, events, MAX_EVENTS, -1);
        if (n_events < 0){
            if (errno == EINTR) continue;
            perror("epoll_wait() failed.");
            break;
        }

        for (int i = 0; i < n_events; i++){
            if (events[i].data.fd == reactor->listen_fd){
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept4(reactor->listen_fd,
                                        (struct sockaddr *)&client_addr,
                                        &addr_len,
                                        SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (client_fd < 0){
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("accept4 failed"); 
                    }
                    continue;
                }

                // ClientContext 해제, 생성: malloc/free (추후 Memory Pool로)
                ClientContext* ctx = malloc(sizeof(ClientContext));
                if (ctx == NULL){
                    perror("ClientContext malloc failed");
                    close(client_fd);
                    continue;
                }
                
                // Context 초기화
                memset(ctx, 0, sizeof(ClientContext)); // 0으로 밀어서 쓰레기값 방지
                ctx->client_fd = client_fd;
                ctx->last_active = time(NULL);
                ctx->state = STATE_REQ_RECEIVING;
                ctx->file_fd = -1;

                // 클라이언트 ip 저장 (로그용)
                inet_ntop(AF_INET, &client_addr.sin_addr, ctx->client_ip, INET_ADDRSTRLEN);
                printf("New Connection: %s (FD: %d)\n", ctx->client_ip, ctx->client_fd);

                struct epoll_event client_event = {0};
                client_event.data.ptr = ctx;
                client_event.events = EPOLLIN | EPOLLONESHOT;

                if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event)){
                    perror("client: epoll_ctl failed");
                    close(client_fd);
                    free(ctx);
                    continue;
                }
            } // listen fd
            else if (events[i].data.fd != reactor->listen_fd) { 
                ClientContext *ctx = (ClientContext*)events[i].data.ptr;

                // 이미 처리 중이면 continue (Race Condition 방지)
                if(ctx->state == STATE_PROCESSING) continue;
                
                ctx->state = STATE_PROCESSING;
                ctx->last_active = time(NULL); // 활동 시간 갱신

                thread_pool_submit(reactor->pool, handle_client_event, ctx);
            }
        }// for
    } // while(true)
    printf("Reactor loop finished.\n");
}


void reactor_stop(Reactor* reactor){
    if(reactor) reactor->running = false;
}


void reactor_destroy(Reactor* reactor){
    if(!reactor) return;

    if (reactor->listen_fd > 0) {
        close(reactor->listen_fd);
        reactor->listen_fd = -1;
    }

    if (reactor->epoll_fd > 0) {
        close(reactor->epoll_fd);
        reactor->epoll_fd = -1;
    }
    // thread_pool은 main에서 정리
}