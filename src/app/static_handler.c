#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "app/static_handler.h"
#include "app/http_utils.h"
#include "app/client_context.h"
#include "core/reactor.h"

static const char* get_mime_type(const char* path);
static HttpResult start_static_transfer(ClientContext *ctx);
static void send_static_header(ClientContext *ctx);
static void send_static_body(ClientContext *ctx);

void handle_static_request(ClientContext* ctx) {
    if (ctx->state == STATE_REQ_RECEIVING || ctx->state == STATE_PROCESSING) {
        HttpResult ret = start_static_transfer(ctx);
        if (ret != RESULT_OK) {
            // 에러 발생 시 즉시 에러 응답 전송 후 종료
            // (내부적으로 404, 403, 500 등에 따라 처리)
            int status_code = (ret == ERR_NOT_FOUND) ? 404 :
                              (ret == ERR_FORBIDDEN) ? 403 : 500;
            send_error_response(ctx, status_code);
            return;
        }
    }

    // 헤더 전송 중 (EAGAIN 후 재진입)
    if (ctx->state == STATE_RES_SENDING_HEADER) {
        send_static_header(ctx);
    }

    // 바디 전송 중 (EAGAIN 후 재진입)
    // 주의: 헤더 전송이 끝난 직후 바로 바디 전송을 시도하므로 if문이 연달아 배치됨
    if (ctx->state == STATE_RES_SENDING_BODY) {
        send_static_body(ctx);
    }
}

static HttpResult start_static_transfer(ClientContext *ctx) {
    // 파일 오픈
    int fd = open(ctx->request_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) return ERR_NOT_FOUND;
        if (errno == EACCES) return ERR_FORBIDDEN;
        perror("static: open failed");
        return ERR_INTERNAL_SERVER;
    }

    // 파일 정보 확인
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("static: fstat failed");
        close(fd);
        return ERR_INTERNAL_SERVER;
    }
//////////////////////////////////////////
    // 디렉토리인 경우 거부 (보안) -> 추후 index.html 자동 매핑은 route_request에서 처리됨/////////////////
    if (S_ISDIR(st.st_mode)) { /////////// 확인 필요
        close(fd);
        return ERR_FORBIDDEN;
    }
    //////////////////////////////////////////////////

    // Context 설정
    ctx->file_fd = fd;
    ctx->file_offset = 0; // 처음부터
    ctx->bytes_remaining = st.st_size; // 전체 크기

    // MIME Type 결정
    const char* mime_type = get_mime_type(ctx->request_path);

    // 헤더 버퍼 작성
    ctx->buffer_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        mime_type, st.st_size
    );
    ctx->buffer_sent = 0;
    
    // 상태 변경 -> 헤더 전송 시작
    ctx->state = STATE_RES_SENDING_HEADER;
    
    return RESULT_OK;
}

static void send_static_header(ClientContext *ctx) {
    int to_send = ctx->buffer_len - ctx->buffer_sent;

    if (to_send <= 0) {
        ctx->state = STATE_RES_SENDING_BODY;
        return;
    }
    ssize_t sent = send(ctx->client_fd, ctx->buffer + ctx->buffer_sent, to_send, 0);

    if (sent > 0) {
        ctx->buffer_sent += sent;
        if (ctx->buffer_sent >= ctx->buffer_len) {
            ctx->state = STATE_RES_SENDING_BODY;
            
            // 헤더 다 보냈으니 바로 바디 전송 시도
        }
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 소켓 버퍼 꽉 참 -> epoll이 나중에 다시 깨워주길 기다림 (Rearm 필요)
            reactor_update_event(ctx->epoll_fd, ctx->client_fd,
                                EPOLLOUT | EPOLLONESHOT, ctx);
            return;
        }
        perror("static: header send failed");
        send_error_response(ctx, 500);
    }
}

static void send_static_body(ClientContext *ctx) {
    off_t offset = ctx->file_offset;
    ssize_t sent = sendfile(ctx->client_fd, ctx->file_fd, &offset, ctx->bytes_remaining);

    if (sent > 0) {
        ctx->bytes_remaining -= sent;
        ctx->file_offset = offset;

        if (ctx->bytes_remaining <= 0) {
            close(ctx->file_fd);
            ctx->file_fd = -1;

            ctx->state = STATE_REQ_RECEIVING;

            ctx->buffer_len = 0;
            ctx->buffer_sent = 0;
            reactor_update_event(ctx->epoll_fd, ctx->client_fd, 
                                 EPOLLIN | EPOLLONESHOT, ctx);
            
            printf("Complete response for: %s\n", ctx->request_path);
            return;
        }
        else {
            // 아직 덜 보냄 (대용량 파일 등) -> 계속 보낼 수 있으면 좋겠지만
            // 워커 스레드 점유를 막기 위해 한 번 보내고 양보하거나,
            // 여기서는 일단 루프 없이 리턴하여 다음 이벤트를 기다리거나 
            // 또는 sendfile 특성상 한 번에 최대한 보냄. 
            // 만약 여기서 리턴하면, Reactor가 다시 호출해주기 위해 EPOLLOUT 유지 필요?
            // -> Level Trigger라면 계속 호출되겠지만, ONESHOT이므로 재장전 필요.
            // -> 하지만 sent > 0 이면 소켓이 아직 열려있을 확률 높음. 
            // -> 보통은 while 루프로 EAGAIN 뜰 때까지 보내는 게 정석이나, 
            // -> 공평성을 위해 여기서는 재장전 후 리턴 (또는 reactor 구조상 바로 리턴하면 다시 wait로 감)
            
            // [수정 전략] 보내는데 성공했으면, 소켓 버퍼가 비었을 수 있으므로 
            // 즉시 다시 EPOLLOUT을 걸어주어 곧바로 다시 호출되게 함.
            reactor_update_event(ctx->epoll_fd, ctx->client_fd, 
                                 EPOLLOUT | EPOLLONESHOT, ctx);
        }
    } else if (sent == 0) {
        send_error_response(ctx, 500);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 소켓 버퍼 꽉 참 -> 쓰기 가능해지면 알려줘
            reactor_update_event(ctx->epoll_fd, ctx->client_fd, 
                                 EPOLLOUT | EPOLLONESHOT, ctx);
            return;
        }
        perror("static: sendfile failed");
        send_error_response(ctx, 500);
    }
}

static const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream"; // 확장자 없음

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0)  return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".mp4") == 0) return "video/mp4";

    return "application/octet-stream"; // 기본값 (다운로드 유도)
}