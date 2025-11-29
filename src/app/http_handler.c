#define _POSIX_C_SOURCE 200809L
// http_parser.c의 진화.        파싱 + 요청 처리

/*
# 역할: 들어온 데이터가 HTTP인지 확인하고, 적절한 담당자에게 넘겨주는 '라우터(Router)'.

# 핵심 기능:
- parse_request(buffer): 들어온 문자열을 분석해 GET /video.mp4 HTTP/1.1 같은 정보를 구조체로 만듦.
- route_request(request):
    URL이 /login이면 -> auth_handler 호출.
    URL이 /watch이면 -> stream_handler 호출.
    URL이 /이면 -> index.html 읽어서 전송.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include "app/http_handler.h"
#include "app/stream_handler.h"
#include "app/client_context.h"

static const enum {
    READ_BLOCK = -2,
    READ_ERR = -1,
    READ_EOF = 0
};

static int try_read_request (ClientContext *ctx);
static int parse_request (ClientContext *ctx);
static void route_request (ClientContext *ctx);
static void rearm_epoll (ClientContext *ctx);

void handle_http_request(void *arg) {
    ClientContext *ctx = (ClientContext*)arg;

    int read_status = try_read_request(ctx);

    if (read_status == READ_ERR) {
        close(ctx->client_fd);
        free(ctx);
        return;
    }
    if (read_status == READ_BLOCK) {
        printf("Client closed connection.\n");
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
            // 에러 X. 데이터 없음
            return READ_BLOCK;
        }
        perror("recv() failed.");
        return READ_ERR;
    }
}

static int parse_request(ClientContext *ctx) {
    // 헤더 끝 찾기
    char *header_end = strstr(ctx->buffer, "\r\n\r\n");

    if (!header_end){
        return -1;
    }
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
    ctx->file_offset = 0;

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
                // long end = 0; // 필요하면 사용

                // '-' 뒤의 값은 무시하고 시작점만 가져옴
                if (sscanf(range_val, "%ld-", &start) == 1) {
                    ctx->file_offset = (off_t)start;
                    // printf("Range Request Detected: Start=%ld\n", start);
                }
            } // if "bytes="
        } // if "Range:"

        // 다른 헤더(Content-Length 등)도 여기서 추가 파싱 가능

    } // while (line 파싱)

    return 0;
}

static void route_request(ClientContext *ctx) {

}

static void rearm_epoll(ClientContext *ctx) {

}