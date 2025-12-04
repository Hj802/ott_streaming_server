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
#include "core/reactor.h"
#include "app/stream_handler.h"
#include "app/http_utils.h"
#include "app/client_context.h"

static HttpResult start_streaming(ClientContext *ctx);
static void continue_sending_header(ClientContext *ctx);
static void continue_sending_file(ClientContext *ctx);

void handle_streaming_request(ClientContext *ctx){
    if (ctx->state == STATE_REQ_RECEIVING || ctx->state == STATE_PROCESSING) {

        HttpResult ret = start_streaming(ctx);
        
        if (ret != RESULT_OK) {
            switch (ret) {
                case ERR_NOT_FOUND: 
                    send_error_response(ctx, -404); 
                    break;
                case ERR_FORBIDDEN: 
                    send_error_response(ctx, -403); 
                    break;
                case ERR_RANGE_NOT_SATISFIABLE: 
                    send_error_response(ctx, -416); 
                    break;
                default: 
                    send_error_response(ctx, -500); 
                    break;
            }
            return; // 에러 전송 후 종료 (send_error 내부에서 close/free 됨)
        }
    } // STATE_REQ_RECEIVING || STATE_PROCESSING

    if (ctx->state == STATE_RES_SENDING_HEADER){
        continue_sending_header(ctx);
    }
    if (ctx->state == STATE_RES_SENDING_BODY){
        continue_sending_file(ctx);
    }
}

static HttpResult start_streaming(ClientContext *ctx) {
    int fd = open(ctx->request_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        // 권한 처리 및 에러 구분
        if (errno == ENOENT) return ERR_NOT_FOUND;  // 404
        if (errno == EACCES) return ERR_FORBIDDEN;  // 403
        perror("open failed");
        return ERR_INTERNAL_SERVER; // 500
    }

    // 파일 정보 획득
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat failed");
        close(fd);
        return ERR_INTERNAL_SERVER; // 500
    }
    
    // 유효성 검사
    off_t total_size = st.st_size;
    if (ctx->range_start >= total_size) {
        return ERR_RANGE_NOT_SATISFIABLE; // 416
    }

    // 범위 계산
    off_t file_end;
    if (ctx->range_end == -1 || ctx->range_end >= total_size) {
        // 끝이 없거나, 파일 크기보다 더 큰 값을 요구했으면 -> 파일 끝으로 맞춤
        file_end = total_size - 1;
    } else {
        // 유효한 끝 범위라면 그대로 사용
        file_end = ctx->range_end;
    }

    size_t content_length = file_end - ctx->range_start + 1;

    // Context에 저장
    ctx->file_fd = fd;
    ctx->file_offset = ctx->range_start;
    ctx->bytes_remaining = content_length;

    // HTTP 헤더 생성
    ctx->buffer_len = 0;
    ctx->buffer_sent = 0;

    int len = snprintf(ctx->buffer, sizeof(ctx->buffer),
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Type: video/mp4\r\n"
        "Content-Range: bytes %ld-%ld/%ld\r\n"
        "Content-Length: %lu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n", // 헤더 끝
        ctx->range_start, file_end, total_size,
        content_length
    );

    if (len < 0 || len >= sizeof(ctx->buffer)) {
        fprintf(stderr, "Header too long\n");
        return ERR_INTERNAL_SERVER; // 500
    }
    ctx->buffer_len = len;

    // 상태 변경
    ctx->state = STATE_RES_SENDING_HEADER;

    printf("[Stream] File: %s, Range: %ld-%ld, Size: %lu\n", 
           ctx->request_path, ctx->range_start, file_end, content_length);

    return RESULT_OK;
}

static void continue_sending_header(ClientContext *ctx) {
    int to_send = ctx->buffer_len - ctx->buffer_sent;

    if (to_send <= 0) {
        ctx->state = STATE_RES_SENDING_BODY;
        return;
    }

    ssize_t sent = send(ctx->client_fd, 
                        ctx->buffer + ctx->buffer_sent, 
                        to_send, 
                        0);
    
    if (sent > 0) {
        ctx->buffer_sent += sent;
        // 헤더 전송 완료 체크
        if (ctx->buffer_sent >= ctx->buffer_len) {
            ctx->state = STATE_RES_SENDING_BODY;
            // 여기서 return하지 않고, 가능하다면 바로 파일 전송 시도 (최적화)
        }
    } else if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 소켓 버퍼 꽉 참 -> 다음 EPOLLOUT 대기
            reactor_update_event(ctx->epoll_fd, ctx->client_fd, 
                                 EPOLLOUT | EPOLLONESHOT, ctx);
            return;
        }
        // 에러 -> 연결 종료
        send_error_response(ctx, 500); // 내부에서 close/free 됨
    }
}

static void continue_sending_file(ClientContext *ctx) {
    // sendfile(out_fd, in_fd, offset_ptr, count)
    // - ctx->file_offset은 포인터로 전달되므로, 커널이 전송한 만큼 값을 증가시켜 줍니다.
    // - Non-blocking 소켓이므로 count(bytes_remaining)가 아무리 커도 
    //   소켓 버퍼가 허용하는 만큼만 보내고 반환됩니다. (Thread Blocking 없음)
    ssize_t sent = sendfile(ctx->client_fd, ctx->file_fd, 
                            &ctx->file_offset, ctx->bytes_remaining);

    if (sent > 0) {
        ctx->bytes_remaining -= sent;

        if (ctx->bytes_remaining == 0) {
            // [전송 완료]
            // 1. 파일 자원 해제
            close(ctx->file_fd);
            ctx->file_fd = -1;

            // 2. 상태 초기화 (다음 요청 대기)
            ctx->state = STATE_REQ_RECEIVING;
            ctx->buffer_len = 0; // 요청 버퍼 리셋
            ctx->buffer_sent = 0;

            // 3. 로그 출력
            printf("[Stream] Completed: %s (Client: %s)\n", ctx->request_path, ctx->client_ip);

            // 4. 감시 모드 변경: 말하기(OUT) -> 듣기(IN)
            // 이제 클라이언트가 보낼 다음 HTTP 요청을 기다립니다.
            if (reactor_update_event(ctx->epoll_fd, ctx->client_fd, 
                                     EPOLLIN | EPOLLONESHOT, ctx) < 0) {
                perror("stream: rearm epollin failed");
                close(ctx->client_fd);
                free(ctx);
            }
        } 
        else {
            // [전송 진행 중]
            // 소켓 버퍼가 찼거나 한번에 다 못 보냄 -> 다시 쓰기 가능해지면 호출해달라고 요청
            // 공평성(Fairness)을 위해 루프를 돌지 않고 양보(Yield)합니다.
            if (reactor_update_event(ctx->epoll_fd, ctx->client_fd, 
                                     EPOLLOUT | EPOLLONESHOT, ctx) < 0) {
                perror("stream: rearm epollout failed");
                close(ctx->file_fd);
                close(ctx->client_fd);
                free(ctx);
            }
        }
    } 
    else if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // [대기] 소켓 버퍼 꽉 참
            if (reactor_update_event(ctx->epoll_fd, ctx->client_fd, 
                                     EPOLLOUT | EPOLLONESHOT, ctx) < 0) {
                perror("stream: rearm epollout failed (EAGAIN)");
                close(ctx->file_fd);
                close(ctx->client_fd);
                free(ctx);
            }
        } else {
            // [에러] 연결 끊김(EPIPE) 등
            // sendfile 에러는 복구 불가능한 경우가 많음
            perror("stream: sendfile error");
            // 내부에서 close(file_fd), close(client_fd), free(ctx) 수행
            send_error_response(ctx, 500); 
        }
    } 
    else { // sent == 0
        // [예외] 파일의 끝에 도달하지 않았는데 0이 리턴됨? (거의 발생 안 함)
        // EOF 처리는 위에서 bytes_remaining으로 하므로, 여기는 비정상 상황
        fprintf(stderr, "stream: unexpected EOF from sendfile\n");
        send_error_response(ctx, 500);
    }
}