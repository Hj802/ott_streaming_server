#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
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
    else if (ctx->state == STATE_RES_SENDING_HEADER){
        continue_sending_header(ctx);
    }
    else if (ctx->state == STATE_RES_SENDING_BODY){
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
        close(fd);
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
        close(fd);
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

}

static void continue_sending_file(ClientContext *ctx) {

}