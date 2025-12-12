#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include "app/http_handler.h"
#include "app/stream_handler.h"
#include "app/http_utils.h"
#include "app/client_context.h"
#include "app/static_handler.h"
#include "app/auth_handler.h"
#include "app/session_manager.h"
#include "app/db_handler.h"
#include "core/reactor.h"

static const enum {
    READ_BLOCK = -2,
    READ_ERR = -1,
    READ_EOF = 0
};

enum ParseResult {
    PARSE_OK = 0,
    PARSE_INCOMPLETE = -1, // 더 읽어야 함
    PARSE_ERROR = -2       // 형식이 잘못됨 (400 Bad Request)
};

static int try_read_request (ClientContext *ctx);
static int parse_request (ClientContext *ctx);
static void route_request (ClientContext *ctx);
static void rearm_epoll (ClientContext *ctx);

void handle_http_request(ClientContext *ctx) {
    int read_status = try_read_request(ctx);

    if (read_status == READ_ERR) {
        printf("Client error: %s\n", ctx->client_ip);
        close(ctx->client_fd);
        free(ctx);
        return;
    }
    if (read_status == READ_EOF) {
        printf("[Info] Client %d closed connection (EOF)\n", ctx->client_fd);
        close(ctx->client_fd);
        free(ctx);
        return;
    }
    if (read_status == READ_BLOCK) {
        // 데이터가 아직 덜 옴: ONESHOT 재설정
        rearm_epoll(ctx); 
        return;
    }


    int parse_result = parse_request(ctx);

    if (parse_result == PARSE_INCOMPLETE) {
        // 헤더 미완성 -> 더 읽기 위해 대기
        rearm_epoll(ctx);
        return;
    }
    else if (parse_result == PARSE_ERROR) {
        // 형식 오류 -> 400 에러 보내고 즉시 종료
        send_error_response(ctx, 400); 
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
    if (!header_end) return PARSE_INCOMPLETE;

    // 바디 시작점
    ctx->body_ptr = header_end + 4;
    *header_end = '\0';

    // request 라인 파싱
    char *saveptr;
    char *line = strtok_r(ctx->buffer, "\r\n", &saveptr);
    
    if(!line) return PARSE_ERROR;

    char method_str[16] = {0};
    char path_str[512] = {0};
    char proto_str[16] = {0};

    if (sscanf(line, "%15s %511s %15s", method_str, path_str, proto_str) != 3) {
        fprintf(stderr, "Malformed HTTP Request\n");
        return PARSE_ERROR;
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

        // Cookie 헤더 파싱
        if (strncasecmp(line, "Cookie:", 7) == 0) {
            char *p = line + 7;
            // "session_id=" 문자열 검색
            char *sess_ptr = strstr(p, "session_id=");
            if (sess_ptr) {
                sess_ptr += 11; // "session_id=" 길이만큼 이동
                
                // 값 추출 (세미콜론, 줄바꿈, 공백 등을 만날 때까지)
                int i = 0;
                while (i < 32 && sess_ptr[i] != '\0' && sess_ptr[i] != ';' && 
                       sess_ptr[i] != '\r' && sess_ptr[i] != '\n' && sess_ptr[i] != ' ') {
                    ctx->session_id[i] = sess_ptr[i];
                    i++;
                }
                ctx->session_id[i] = '\0';
            }
        }
    } // while (line 파싱)
    return PARSE_OK;
}

static void route_request(ClientContext *ctx) {
    if (strstr(ctx->request_path, "..")) {
        fprintf(stderr, "[Security] Blocked traversal attempt: %s\n", ctx->request_path);
        send_error_response(ctx, ERR_FORBIDDEN);
        return;
    }
    // [API 처리] 로그인 처리
    if (strcmp(ctx->request_path, "/login") == 0 && ctx->method == HTTP_POST) {
        handle_login(ctx);
        return;
    }

    // [API 처리] 로그아웃 (POST /logout)
    if (strcmp(ctx->request_path, "/logout") == 0 && ctx->method == HTTP_POST) {
        handle_logout(ctx); // auth_handler.c
        return;
    }

    // [API 처리] 회원가입 (POST /register)
    if (strcmp(ctx->request_path, "/register") == 0 && ctx->method == HTTP_POST) {
        handle_register(ctx);
        return;
    }

    // [API 처리] 시청 이력 저장 (POST /api/history)
    if (strcmp(ctx->request_path, "/api/history") == 0 && ctx->method == HTTP_POST) {
        handle_api_history(ctx);
        return;
    }
    

    // [API 처리] 비디오 목록 (GET /api/videos)
    if (strcmp(ctx->request_path, "/api/videos") == 0 && ctx->method == HTTP_GET) {
        if (strlen(ctx->session_id) == 0 || session_get_user(ctx->session_id) < 0) {
            send_error_response(ctx, 401); // Unauthorized 반환
            return;
        }
        handle_api_video_list(ctx);
        return;
    }

    char file_path[512] = {0};

    // [경로 매핑] 루트 경로("/") -> "static/index.html"
    if (strcmp(ctx->request_path, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "static/index.html");
    }
    // [경로 매핑] 나머지 정적 파일들 
    // 앞의 '/'를 제거하고 'static/'을 붙임
    else if (ctx->request_path[0] == '/') {
        // 확장자 추출
        const char *ext = strrchr(ctx->request_path, '.');
        
        // 정적 자원(Static)인지 확인
        int is_static = 0;
        if (ext && (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".css") == 0 || 
                    strcasecmp(ext, ".js") == 0   || strcasecmp(ext, ".png") == 0 || 
                    strcasecmp(ext, ".jpg") == 0  || strcasecmp(ext, ".ico") == 0)) {
            is_static = 1;
        }

        // 경로 조립
        if (is_static) {
            // 이미 /static/으로 시작하는지 확인 (중복 방지)
            if (strncmp(ctx->request_path, "/static/", 8) == 0) {
                 snprintf(file_path, sizeof(file_path), ".%s", ctx->request_path); // ./static/...
            } else {
                 snprintf(file_path, sizeof(file_path), "static%s", ctx->request_path);
            }
        } else {
            // MP4 등 미디어 파일은 루트(혹은 media 폴더)에서 찾음
            // 예: /test.mp4 -> ./test.mp4
            snprintf(file_path, sizeof(file_path), ".%s", ctx->request_path);
        }
    }

    // 최종 경로 업데이트
    strncpy(ctx->request_path, file_path, sizeof(ctx->request_path) - 1);

    const char *ext = strrchr(ctx->request_path, '.');
    if (ext && strcasecmp(ext, ".mp4") == 0) {
        // [세션 검증]
        // 쿠키가 없거나 유효하지 않으면 거부
        if (strlen(ctx->session_id) == 0 || session_get_user(ctx->session_id) < 0) {
            printf("[Access] Denied for %s (Invalid Session)\n", ctx->client_ip);
            send_error_response(ctx, 401); // 401 Unauthorized
            return;
        }
        // 인증 성공 -> 스트리밍 진행
    }

    // [파일 핸들러 분배] 확장자 기반
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