#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include "app/http_handler.h"
#include "app/stream_handler.h"
#include "app/http_utils.h"
#include "app/client_context.h"
#include "core/reactor.h"

static const enum {
    READ_BLOCK = -2,
    READ_ERR = -1,
    READ_EOF = 0
};

static int try_read_request (ClientContext *ctx);
static int parse_request (ClientContext *ctx);
static void route_request (ClientContext *ctx);
static void rearm_epoll (ClientContext *ctx);

void handle_http_request(ClientContext *ctx) {
    int read_status = try_read_request(ctx);

    if (read_status == READ_ERR) {
        close(ctx->client_fd);
        free(ctx);
        return;
    }
    if (read_status == READ_BLOCK) {
        printf("Client closed connection: %s\n", ctx->client_ip);
        close(ctx->client_fd);
        free(ctx);
        return;
    }
    if (read_status == READ_BLOCK) {
        // 데이터가 아직 덜 옴: ONESHOT 재설정
        rearm_epoll(ctx); 
        return;
    }

    if (parse_request(ctx) < 0) {
        // 헤더가 아직 완성 되지 않음: 대기
        rearm_epoll(ctx);
        return;
    }
    route_request(ctx);
}

static int try_read_request(ClientContext *ctx) {
    int remaining = (sizeof(ctx->buffer) - 1) - ctx->buffer_len;

    if (remaining <= 0){
        fprintf(stderr, "Error: Request Header too large (Buffer Full)\n");
        return READ_ERR;
    }

    char *ptr = ctx->buffer + ctx->buffer_len;
    ssize_t received = recv(ctx->client_fd, ptr, remaining, 0);

    if (received > 0){
        ctx->buffer_len += received;
        ctx->buffer[ctx->buffer_len] = '\0';
        return received;
    } 
    else if (received == 0) {
        // FIN
        return READ_EOF;
    } 
    else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return READ_BLOCK;
        }
        perror("recv() failed.");
        return READ_ERR;
    }
}

static int parse_request(ClientContext *ctx) {
    // 헤더 끝 찾기
    char *header_end = strstr(ctx->buffer, "\r\n\r\n");
    if (!header_end) return -1;
    *header_end = '\0';
    // 바디 시작점
    // char *body_start = header_end + 4;

    // request 라인 파싱
    char *saveptr;
    char *line = strtok_r(ctx->buffer, "\r\n", &saveptr);
    
    if(!line) return -1;

    char method_str[16] = {0};
    char path_str[512] = {0};
    char proto_str[16] = {0};

    if (sscanf(line, "%15s %511s %15s", method_str, path_str, proto_str) != 3) {
        fprintf(stderr, "Malformed HTTP Request\n");
        return -1;
    }

    if (strcmp(method_str, "GET") == 0) ctx->method = HTTP_GET;
    else if (strcmp(method_str, "POST") == 0) ctx->method = HTTP_POST;
    else if (strcmp(method_str, "OPTIONS") == 0) ctx->method = HTTP_OPTIONS;
    else ctx->method = HTTP_UNKNOWN;

    // path 저장
    strncpy(ctx->request_path, path_str, sizeof(ctx->request_path) - 1);
    ctx->request_path[sizeof(ctx->request_path) - 1] = '\0';

    // Default Offset 초기화
    ctx->range_start = 0;
    ctx->range_end = -1;

    // 헤더 라인 파싱 (Range 찾기) 
    while ((line = strtok_r(NULL, "\r\n", &saveptr)) != NULL) {
        
        // 대소문자 무시하고 Range 헤더 찾기 (strncasecmp)
        if (strncasecmp(line, "Range:", 6) == 0) {
            // 공백 건너뛰기
            char *value = line + 6;
            while (*value == ' ') value++;

            // Open-ended Range 형식 파싱
            if (strncasecmp(value, "bytes=", 6) == 0) {
                char *range_val = value + 6;
                long start = 0;
                long end = 0;

                // Closed
                if (sscanf(range_val, "%ld-%ld", &start, &end) == 2) {
                    ctx->range_start = (off_t)start;
                    ctx->range_end   = (off_t)end;
                } 
                // Open-ended
                else if (sscanf(range_val, "%ld-", &start) == 1) {
                    ctx->range_start = (off_t)start;
                    ctx->range_end   = -1; // 파일 끝까지
                }
            } // if "bytes="
        } // if "Range:"

        // TODO: Cookie 헤더 파싱 추가 위치

    } // while (line 파싱)
    return 0;
}

static void route_request(ClientContext *ctx) {
    if (strstr(ctx->request_path, "..")) {
        fprintf(stderr, "[Security] Blocked traversal attempt: %s\n", ctx->request_path);
        send_error_response(ctx, ERR_FORBIDDEN);
        return;
    }

    if (strcmp(ctx->request_path, "/") == 0) {
        strncpy(ctx->request_path, "/index.html", sizeof(ctx->request_path) - 1);
    }

    // 3. [API 처리] 로그인/로그아웃 등 로직 처리
    if (strcmp(ctx->request_path, "/login") == 0 && ctx->method == HTTP_POST) {
        // handle_login_request(ctx); // auth_handler.c 구현 필요
        return;
    }

    // TODO: video_list

    // TODO: POST

    // TODO: OPTIONS
    
    // TODO: [접근 제어] 보호된 리소스(.mp4) 인증 확인
    // (추후 Session Manager 구현 시 주석 해제)
    /*
    const char *ext = strrchr(ctx->request_path, '.');
    if (ext && strcasecmp(ext, ".mp4") == 0) {
        // 쿠키 확인 로직
        // if (!check_session(ctx)) {
        //     send_error_response(ctx, 401); // or Redirect
        //     return;
        // }
    }
    */

    // 5. [파일 핸들러 분배] 확장자 기반
    const char *ext = strrchr(ctx->request_path, '.');
    if (ext) {
        if (strcasecmp(ext, ".mp4") == 0) {
            // 동영상 스트리밍 (Range 지원)
            handle_streaming_request(ctx);
        } 
        else if (strcasecmp(ext, ".html") == 0 || 
                 strcasecmp(ext, ".css") == 0 ||
                 strcasecmp(ext, ".js") == 0 ||
                 strcasecmp(ext, ".png") == 0 ||
                 strcasecmp(ext, ".jpg") == 0 ||
                 strcasecmp(ext, ".ico") == 0) {
            // 정적 파일 전송 (단순 전송)
            handle_static_request(ctx);
        } 
        else {
            // 지원하지 않는 파일 형식
            send_error_response(ctx, ERR_NOT_FOUND);
        }
    } else {
        // 확장자가 없는 경우 (API도 아니고 파일도 아님)
        send_error_response(ctx, ERR_NOT_FOUND);
    }

}

static void rearm_epoll(ClientContext *ctx) {
    int events = EPOLLONESHOT;

    switch (ctx->state) {
        case STATE_REQ_RECEIVING:
        case STATE_PROCESSING:
            // 요청을 읽는 중이거나 파싱 중이면 -> 읽기 감시
            events |= EPOLLIN;
            break;
        
        case STATE_RES_SENDING_HEADER:
        case STATE_RES_SENDING_BODY:
            // 응답을 보내는 중이면 -> 쓰기 감시 (버퍼 빔 대기)
            events |= EPOLLOUT;
            break;
            
        default:
            // 기본은 읽기
            events |= EPOLLIN;
            break;
    }
    
    if (reactor_update_event(ctx->epoll_fd, ctx->client_fd, events, ctx) < 0) {
        // epoll 등록 실패 시 연결 종료 (치명적 오류)
        perror("rearm_epoll failed");
        close(ctx->client_fd);
        free(ctx);
    }
}