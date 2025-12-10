#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/epoll.h>
#include "app/auth_handler.h"
#include "app/http_utils.h"
#include "app/db_handler.h"
#include "app/session_manager.h"
#include "app/client_context.h"
#include "core/reactor.h"

#define JSON_LOGIN_SUCCESS "{\"success\": true}"
#define JSON_LOGIN_FAIL    "{\"success\": false, \"message\": \"Invalid credentials\"}"
#define JSON_INTERNAL_ERR  "{\"success\": false, \"message\": \"Internal server error\"}"

void handle_login(ClientContext *ctx) {
    // 1. POST 바디 위치 찾기 (헤더 파싱 시 \r\n\r\n 위치 이후)
    const char *body = ctx->buffer + strlen(ctx->buffer) + 4;

    char username[64] = {0};
    char password[64] = {0};

    // 2. 파싱 유틸리티 사용 (http_utils)
    if (http_get_form_param(body, "username", username, sizeof(username)) < 0 ||
        http_get_form_param(body, "password", password, sizeof(password)) < 0) {
        send_error_response(ctx, 400); // Bad Request
        return;
    }

    // 3. DB 자격 증명 확인 (db_handler)
    int user_id = db_verify_user(username, password);
    char final_response[1024] = {0};
    int header_len = 0;

    if (user_id <= 0) {
        // 실패 시 JSON 응답
        int h_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n\r\n", strlen(JSON_LOGIN_FAIL));
        
        send_all_blocking(ctx->client_fd, ctx->buffer, header_len);
        send_all_blocking(ctx->client_fd, JSON_LOGIN_FAIL, strlen(JSON_LOGIN_FAIL));
    } else {
        // 세션 생성 (session_manager)
        char session_id[SESSION_ID_LENGTH];
        if (session_create(user_id, session_id, sizeof(session_id)) < 0) {
            send_error_response(ctx, 500);
            return;
        }

        // 성공 응답 구성 (Set-Cookie 포함)
        int h_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Set-Cookie: session_id=%s; Path=/; HttpOnly; Max-Age=%d\r\n"
            "\r\n", 
            strlen(JSON_LOGIN_SUCCESS), session_id, SESSION_TTL_SEC);

        // 전송, 헤더와 바디 전송 로직 분리 및 완결성 체크
        if (send_all_blocking(ctx->client_fd, ctx->buffer, header_len) == 0) {
            send_all_blocking(ctx->client_fd, JSON_LOGIN_SUCCESS, strlen(JSON_LOGIN_SUCCESS));
        }

        printf("[Auth] User %s logged in. Session: %s\n", username, session_id);
    }

    // 상태 초기화 및 Epoll 재장전
    ctx->state = STATE_REQ_RECEIVING;
    ctx->buffer_len = 0;
    reactor_update_event(ctx->epoll_fd, ctx->client_fd, EPOLLIN | EPOLLONESHOT, ctx);
}

void handle_logout(ClientContext *ctx) {

}
