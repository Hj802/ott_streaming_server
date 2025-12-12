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
#define JSON_LOGOUT_SUCCESS "{\"success\": true, \"message\": \"Logged out\"}"
#define JSON_REG_SUCCESS "{\"success\": true, \"message\": \"User created\"}"
#define JSON_REG_FAIL    "{\"success\": false, \"message\": \"Username already exists\"}"   

void handle_login(ClientContext *ctx) {
    const char *body = ctx->body_ptr;
    if (!body) {
        send_error_response(ctx, 400);
        return;
    }

    char username[64] = {0};
    char password[64] = {0};

    // 파싱 유틸리티 사용
    if (http_get_form_param(body, "username", username, sizeof(username)) < 0 ||
        http_get_form_param(body, "password", password, sizeof(password)) < 0) {
        send_error_response(ctx, 400); // Bad Request
        return;
    }

    // DB 자격 증명 확인 (db_handler)
    int user_id = db_verify_user(username, password);
    char final_response[1024] = {0};
    int header_len = 0;

    if (user_id <= 0) {
        // 실패 시 JSON 응답
        header_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
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
        header_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
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
    if (strlen(ctx->session_id) > 0) {
        session_remove(ctx->session_id);
        // 메모리 상의 ID도 지워줌 (이중 삭제 방지)
        memset(ctx->session_id, 0, sizeof(ctx->session_id));
        printf("[Auth] Session removed via logout.\n");
    }

    // 응답 (쿠키 만료 처리: Max-Age=0)
    int header_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Set-Cookie: session_id=; Path=/; HttpOnly; Max-Age=0\r\n"
        "Connection: keep-alive\r\n\r\n",
        strlen(JSON_LOGOUT_SUCCESS));

    send_all_blocking(ctx->client_fd, ctx->buffer, header_len);
    send_all_blocking(ctx->client_fd, JSON_LOGOUT_SUCCESS, strlen(JSON_LOGOUT_SUCCESS));

    // 3. 재장전
    ctx->state = STATE_REQ_RECEIVING;
    ctx->buffer_len = 0;
    
    if (reactor_update_event(ctx->epoll_fd, ctx->client_fd, EPOLLIN | EPOLLONESHOT, ctx) < 0) {
        close(ctx->client_fd);
        free(ctx);
    }
}

void handle_register(ClientContext *ctx) {
    // 1. 바디 파싱 (로그인과 동일 로직)
    char *body = ctx->body_ptr;
    if (!body) {
        send_error_response(ctx, 400);
        return;
    }

    char username[64] = {0};
    char password[64] = {0};

    if (http_get_form_param(body, "username", username, sizeof(username)) < 0 ||
        http_get_form_param(body, "password", password, sizeof(password)) < 0) {
        send_error_response(ctx, 400);
        return;
    }

    // 2. DB 생성 호출
    int result = db_create_user(username, password);
    int header_len = 0;

    if (result == 0) {
        // 성공
        header_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: keep-alive\r\n\r\n", strlen(JSON_REG_SUCCESS));
        
        send_all_blocking(ctx->client_fd, ctx->buffer, header_len);
        send_all_blocking(ctx->client_fd, JSON_REG_SUCCESS, strlen(JSON_REG_SUCCESS));
        printf("[Auth] New user registered: %s\n", username);
    } else {
        // 실패 (중복 등)
        header_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
            "HTTP/1.1 409 Conflict\r\n" // 409: 리소스 충돌
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: keep-alive\r\n\r\n", strlen(JSON_REG_FAIL));
            
        send_all_blocking(ctx->client_fd, ctx->buffer, header_len);
        send_all_blocking(ctx->client_fd, JSON_REG_FAIL, strlen(JSON_REG_FAIL));
    }

    // 3. 재장전
    ctx->state = STATE_REQ_RECEIVING;
    ctx->buffer_len = 0;
    reactor_update_event(ctx->epoll_fd, ctx->client_fd, EPOLLIN | EPOLLONESHOT, ctx);
}