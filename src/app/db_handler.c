#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sqlite3.h>
#include "app/db_handler.h"
#include "app/client_context.h"

// 데이터베이스 연결 객체 (파일 내부 전역 변수)
// 외부에서는 접근하지 못하도록 static으로 숨깁니다.
static sqlite3 *g_db = NULL;

// 내부 헬퍼 함수
static int seed_initial_data();
static int send_all_blocking(int fd, const char *data, size_t len); // [신규] 안전 전송

int db_init(const char *db_path) {
    int rc = sqlite3_open(db_path, &g_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Cannot open database: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    // =========================================================
    // [튜닝] 성능 및 동시성 최적화 (Review 반영)
    // =========================================================
    char *err_msg = 0;
    
    // 1. WAL Mode: 읽기/쓰기 동시 수행 가능 (락 경합 최소화)
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    
    // 2. Synchronous Normal: 적절한 안정성과 성능 타협
    sqlite3_exec(g_db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);
    
    // 3. Busy Timeout: 락이 걸려있을 때 5초간 대기 (즉시 에러 X)
    sqlite3_busy_timeout(g_db, 5000);

    printf("[DB] Optimized (WAL, Sync=Normal, Timeout=5s)\n");
    // =========================================================

    // 테이블 생성
    const char *sql_create = 
        "CREATE TABLE IF NOT EXISTS videos ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT NOT NULL, "
        "filepath TEXT NOT NULL, "
        "thumbnail TEXT, "
        "duration INTEGER DEFAULT 0"
        ");";

    rc = sqlite3_exec(g_db, sql_create, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Create table failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        return -1;
    }

    const char *sql_create_users = 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT NOT NULL UNIQUE, "
        "password TEXT NOT NULL" // 실제 운영 시 해시값 저장 필수
        ");";

    if (sqlite3_exec(g_db, sql_create_users, 0, 0, 0) != SQLITE_OK) {
        fprintf(stderr, "[DB] Create table failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        return -1;
    }

    // 초기 데이터 주입
    seed_initial_data();
    return 0;
}

void db_cleanup() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

// [내부 함수] 초기 데이터 주입
static int seed_initial_data() {
    int rc;
    sqlite3_stmt *stmt;
    int count = 0;

    // A. 데이터 개수 확인
    const char *sql_count = "SELECT count(*) FROM videos;";
    rc = sqlite3_prepare_v2(g_db, sql_count, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    // 데이터가 이미 있으면 스킵
    if (count > 0) return 0;

    printf("[DB] Seeding initial data...\n");

    // B. 더미 데이터 삽입
    // 주의: filepath는 route_request 로직에 맞춰 '/test.mp4' 등으로 설정해야 함
    const char *sql_insert = 
        "INSERT INTO videos (title, filepath, thumbnail) VALUES "
        "('Test Video (Local)', '/test.mp4', '/static/thumb1.jpg'), "
        "('Iron Man Trailer', '/ironman.mp4', '/static/thumb2.jpg');";

    char *err_msg = 0;
    rc = sqlite3_exec(g_db, sql_insert, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] SQL error (Insert): %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    printf("[DB] Seed data inserted.\n");
    return 0;
}

void handle_api_video_list(ClientContext *ctx) {
    if (!g_db) { send_error_response(ctx, 500); return; }

    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, title, filepath, thumbnail, duration FROM videos";

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) != SQLITE_OK) {
        send_error_response(ctx, 500);
        return;
    }

    size_t json_cap = 16384; 
    char *json_body = (char*)malloc(json_cap);
    if (!json_body) {
        sqlite3_finalize(stmt);
        send_error_response(ctx, 500);
        return;
    }

    char *p = json_body;
    size_t rem = json_cap;
    int len = snprintf(p, rem, "[");
    p += len; rem -= len;

    int is_first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!is_first) { if (rem > 2) { *p++ = ','; rem--; } }
        is_first = 0;

        // (단순화) 컬럼 읽기
        int id = sqlite3_column_int(stmt, 0);
        const char *title = (const char*)sqlite3_column_text(stmt, 1);
        const char *filepath = (const char*)sqlite3_column_text(stmt, 2);
        const char *thumb = (const char*)sqlite3_column_text(stmt, 3);
        int duration = sqlite3_column_int(stmt, 4);

        // [주의] 여기서 title 등에 " 문자가 있으면 JSON 깨짐 (이스케이프 미구현)
        len = snprintf(p, rem, 
            "{\"id\":%d, \"title\":\"%s\", \"url\":\"%s\", \"thumbnail\":\"%s\", \"duration\":%d}",
            id, title?title:"", filepath?filepath:"", thumb?thumb:"", duration);
        
        if (len < 0 || (size_t)len >= rem) break;
        p += len; rem -= len;
    }
    if (rem > 2) { strcpy(p, "]"); }
    sqlite3_finalize(stmt);

    // 헤더 생성
    size_t body_len = strlen(json_body);
    int header_len = snprintf(ctx->buffer, sizeof(ctx->buffer),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n", body_len
    );

    // =========================================================
    // [수정] 안전한 전송 로직 (Partial Send 방지)
    // =========================================================
    
    // 1. 헤더 전송 (Blocking Loop)
    if (send_all_blocking(ctx->client_fd, ctx->buffer, header_len) < 0) {
        perror("[API] Failed to send header");
        free(json_body);
        // 이미 망가졌으므로 연결 종료 처리
        close(ctx->client_fd);
        free(ctx);
        return;
    }

    // 2. 바디 전송 (Blocking Loop)
    if (send_all_blocking(ctx->client_fd, json_body, body_len) < 0) {
        perror("[API] Failed to send JSON body");
        // 로그만 남기고 자원 정리는 아래에서
    } else {
        printf("[API] Sent video list (%zu bytes)\n", body_len);
    }

    free(json_body);

    // 다음 요청 대기 (Rearm)
    // 전송 실패했어도 여기서 EPOLLIN 걸면(소켓 살아있다면) 복구 시도 가능하나,
    // 보통은 send 실패 시 close 하는 게 맞음. 여기서는 성공 가정하에 진행.
    ctx->state = STATE_REQ_RECEIVING;
    ctx->buffer_len = 0;
    
    if (reactor_update_event(ctx->epoll_fd, ctx->client_fd, EPOLLIN | EPOLLONESHOT, ctx) < 0) {
         close(ctx->client_fd);
         free(ctx);
    }
}

int db_verify_user(const char *username, const char *password) {
    if (!g_db) return -2;

    sqlite3_stmt *stmt;
    // SQL Injection 방지를 위한 바인딩 쿼리
    const char *sql = "SELECT id FROM users WHERE username = ? AND password = ?;";
    
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) != SQLITE_OK) {
        return -2;
    }

    // 파라미터 바인딩
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    int user_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return user_id;
}

// 헬퍼 함수: 모든 데이터를 보낼 때까지 반복 (Blocking)
static int send_all_blocking(int fd, const char *data, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(fd, data + total_sent, len - total_sent, 0);
        if (sent > 0) {
            total_sent += sent;
        } else if (sent < 0) {
            if (errno == EINTR) continue; // 시그널 인터럽트는 무시하고 계속
            // [중요] EAGAIN 처리가 딜레마임. 
            // Reactor 패턴에서는 기다려야 하지만, 여기서는 복잡도를 피하기 위해
            // 잠시(Busy Wait or short sleep) 기다리거나 에러로 처리.
            // 현실적으로 JSON 전송 중 버퍼가 꽉 차는 건 드문 일이므로 에러 처리.
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 정말 정교하게 하려면 usleep(1000) 등을 줄 수도 있지만,
                // 여기서는 "Partial Send 발생 시 에러"로 규정.
                // (스트리밍 서버의 API 응답이므로 이 정도 타협은 가능)
                return -1; 
            }
            return -1; // 진짜 에러
        } else {
            return -1; // Connection closed
        }
    }
    return 0; // 성공
}