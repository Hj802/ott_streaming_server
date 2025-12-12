#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "app/history_handler.h"
#include "app/http_utils.h"    // 파싱 및 전송 유틸리티
#include "app/db_handler.h"    // DB 업데이트
#include "app/session_manager.h" // 세션 검증
#include "core/reactor.h"

#define JSON_SUCCESS "{\"success\": true}"
#define JSON_FAIL    "{\"success\": false, \"message\": \"Update failed\"}"
#define JSON_AUTH_FAIL "{\"success\": false, \"message\": \"Unauthorized\"}"

void handle_api_history(ClientContext *ctx) {
    // 1. [보안] 세션 검증 및 User ID 확보
    // 로그인이 안 된 상태에서 기록을 저장할 수는 없음
    int user_id = -1;
    if (strlen(ctx->session_id) > 0) {
        user_id = session_get_user(ctx->session_id);
    }

    if (user_id < 0) {
        // 인증 실패 시 401 리턴
        int len = snprintf(ctx->buffer, sizeof(ctx->buffer),
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Length: %zu\r\n\r\n", strlen(JSON_AUTH_FAIL));
        send_all_blocking(ctx->client_fd, ctx->buffer, len);
        send_all_blocking(ctx->client_fd, JSON_AUTH_FAIL, strlen(JSON_AUTH_FAIL));
        goto finish;
    }

    // 2. Body 파싱
    // http_handler에서 저장해둔 body_ptr 사용
    const char *body = ctx->body_ptr;
    if (!body) {
        send_error_response(ctx, 400);
        goto finish;
    }

    char vid_str[16] = {0};
    char time_str[16] = {0};

    // video_id와 timestamp 추출
    if (http_get_form_param(body, "video_id", vid_str, sizeof(vid_str)) < 0 ||
        http_get_form_param(body, "timestamp", time_str, sizeof(time_str)) < 0) {
        send_error_response(ctx, 400); // 파라미터 누락
        goto finish;
    }

    int video_id = atoi(vid_str);
    int timestamp = atoi(time_str);

    // 3. DB 업데이트 (Write-Back)
    // 여기서 DB를 호출합니다.
    if (db_update_history(user_id, video_id, timestamp) == 0) {
        // 성공
        int len = snprintf(ctx->buffer, sizeof(ctx->buffer),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: keep-alive\r\n\r\n", strlen(JSON_SUCCESS));
        
        send_all_blocking(ctx->client_fd, ctx->buffer, len);
        send_all_blocking(ctx->client_fd, JSON_SUCCESS, strlen(JSON_SUCCESS));
        
        // 너무 자주 찍히면 로그가 지저분하므로 주석 처리하거나 디버그용으로만 사용
        // printf("[History] User %d saved Video %d at %ds\n", user_id, video_id, timestamp);
    } else {
        // DB 에러
        send_error_response(ctx, 500);
    }

finish:
    // 4. 재장전 (Keep-Alive)
    ctx->state = STATE_REQ_RECEIVING;
    ctx->buffer_len = 0;
    reactor_update_event(ctx->epoll_fd, ctx->client_fd, EPOLLIN | EPOLLONESHOT, ctx);
}