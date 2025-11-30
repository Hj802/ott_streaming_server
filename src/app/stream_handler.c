// R6, R8, R9: range 요청, 스트리밍 처리

/*
# 역할: 동영상 스트리밍의 핵심. 가장 복잡한 곳.

# 핵심 기능:
- handle_streaming(filename, range_header):
- Range: bytes=100- 헤더 파싱.
- http_response_header 생성 (206 Partial Content).
- 파일 전송: sendfile() 시스템 콜을 사용하여 커널이 직접 파일을 소켓으로 쏘게 만듦 (Zero-copy).
- R7(이어보기) 저장을 위해 "현재 몇 초까지 봤는지" 정보를 주기적으로 db_handler에 업데이트 요청.
*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "app/stream_handler.h"
#include "include/app/client_context.h"

static const enum {
    SEND_416 = -4,
    SEND_403 = -3,
    SEND_404 = -2,
    FILE_ERR = -1,
    RESULT_OK = 0
};

static int start_streaming(ClientContext *ctx);
static void continue_sending_header(ClientContext *ctx);
static void continue_sending_file(ClientContext *ctx);

void handle_streaming_request(ClientContext *ctx){
    switch (ctx->state)
    {
    case STATE_REQ_RECEIVING:
    case STATE_PROCESSING:
        if (start_streaming(ctx) < 0) {
            // 에러 발생 시 여기서 에러 페이지 전송 로직 호출 필요
                // 예: send_error_response(ctx, 404);
                // 지금은 로깅만 하고 종료
                printf("Streaming Start Failed\n");
                close(ctx->client_fd); // 연결 종료
                return;
        }
        // 성공하면 바로 헤더 전송으로 넘어감 (Fall-through)
        // break; (의도적으로 뺌, 바로 보내기 위해)
    case STATE_RES_SENDING_HEADER:
        continue_sending_header(ctx);
        break;
    case STATE_RES_SENDING_BODY:
        continue_sending_file(ctx);
        break;

    default:
        break;
    } // switch
}

static int start_streaming(ClientContext *ctx) {
    int fd = open(ctx->request_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        // 권한 처리 및 에러 구분
        if (errno == ENOENT) return SEND_404;      // 파일 없음
        if (errno == EACCES) return SEND_403;      // 권한 없음 (Permission denied)
        perror("open failed");
        return FILE_ERR;
    }

    // 파일 정보 획득
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat failed");
        close(fd);
        return FILE_ERR;
    }
    
    // 유효성 검사
    off_t total_size = st.st_size;
    if (ctx->range_start >= total_size) {
        close(fd);
        return SEND_416; // 416 Range Not Satisfiable
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
        return FILE_ERR;
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