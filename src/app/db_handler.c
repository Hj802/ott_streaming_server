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
#include "app/http_utils.h"
#include "app/client_context.h"
#include "core/reactor.h"

#define FFMPEG_CMD_TEMPLATE "ffmpeg -i \"%s\" -ss 00:00:05 -vframes 1 \"%s\" -y > /dev/null 2>&1"

// 데이터베이스 연결 객체 (파일 내부 전역 변수)
// 외부에서는 접근하지 못하도록 static으로 숨깁니다.
static sqlite3 *g_db = NULL;

// 내부 헬퍼 함수
static int seed_initial_data();
static int generate_thumbnail_ffmpeg(const char *video_path, const char *out_thumb_path);

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

    const char *sql_history = 
        "CREATE TABLE IF NOT EXISTS watch_history ("
        "user_id INTEGER,"
        "video_id INTEGER,"
        "last_pos INTEGER DEFAULT 0,"
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY (user_id, video_id)"
        ");";
    
    if (sqlite3_exec(g_db, sql_history, 0, 0, 0) != SQLITE_OK) {
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

    sqlite3_exec(g_db, "INSERT OR IGNORE INTO users (username, password) VALUES ('user1', '1234');", 0, 0, 0);

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

    const char *vid1_phys = "./test.mp4";         // 실제 비디오 파일 위치
    const char *thumb1_phys = "./static/thumb1.jpg"; // 썸네일이 저장될 실제 위치
    
    const char *vid2_phys = "./ironman.mp4";
    const char *thumb2_phys = "./static/thumb2.jpg";

    generate_thumbnail_ffmpeg(vid1_phys, thumb1_phys);
    generate_thumbnail_ffmpeg(vid2_phys, thumb2_phys);

    // B. 더미 데이터 삽입
    // 주의: filepath는 route_request 로직에 맞춰 '/test.mp4' 등으로 설정해야 함
    const char *sql_insert = 
        "INSERT INTO videos (title, filepath, thumbnail) VALUES "
        "('Test Video (Auto Thumb)', '/test.mp4', '/thumb1.jpg'), "
        "('Iron Man (Auto Thumb)', '/ironman.mp4', '/thumb2.jpg');";

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
    // 1. 세션에서 user_id 추출 (이미 http_handler에서 검증했으므로 있다고 가정)
    // 만약 세션이 없으면 user_id = 0 (이력 없음)으로 처리
    int user_id = 0;
    if (strlen(ctx->session_id) > 0) {
        user_id = session_get_user(ctx->session_id);
        if (user_id < 0) user_id = 0;
    }

    // 2. JSON 생성 (Join 쿼리 실행)
    char *json_body = db_get_video_list_json(user_id);
    if (!json_body) {
        send_error_response(ctx, 500);
        return;
    }

    // 3. 전송
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

// 시청 이력 저장 (Upsert)
int db_update_history(int user_id, int video_id, int timestamp) {
    if (!g_db) return -1;
    
    // SQLite의 INSERT OR REPLACE 문법 사용
    const char *sql = "INSERT OR REPLACE INTO watch_history (user_id, video_id, last_pos, updated_at) VALUES (?, ?, ?, CURRENT_TIMESTAMP);";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) != SQLITE_OK) return -1;
    
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, video_id);
    sqlite3_bind_int(stmt, 3, timestamp);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

// 비디오 목록 + 시청 이력 조회 (기존 handle_api_video_list 대체용 헬퍼)
char* db_get_video_list_json(int user_id) {
    if (!g_db) return NULL;

    // LEFT JOIN을 사용하여 시청 기록이 없으면 last_pos가 NULL(0 처리)이 되게 함
    const char *sql = 
        "SELECT v.id, v.title, v.filepath, v.thumbnail, v.duration, "
        "COALESCE(h.last_pos, 0) as last_pos "
        "FROM videos v "
        "LEFT JOIN watch_history h ON v.id = h.video_id AND h.user_id = ? "
        "ORDER BY v.id ASC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) != SQLITE_OK) return NULL;
    
    sqlite3_bind_int(stmt, 1, user_id);

    // JSON 버퍼 (충분히 크게 잡음)
    size_t cap = 16384;
    char *json = (char*)malloc(cap);
    if (!json) { sqlite3_finalize(stmt); return NULL; }
    
    char *p = json;
    size_t rem = cap;
    int len = snprintf(p, rem, "[");
    p += len; rem -= len;

    int is_first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!is_first) { if (rem > 2) { *p++ = ','; rem--; } }
        is_first = 0;

        int id = sqlite3_column_int(stmt, 0);
        const char *title = (const char*)sqlite3_column_text(stmt, 1);
        const char *path = (const char*)sqlite3_column_text(stmt, 2);
        const char *thumb = (const char*)sqlite3_column_text(stmt, 3);
        int duration = sqlite3_column_int(stmt, 4);
        int last_pos = sqlite3_column_int(stmt, 5); // [핵심] 이어보기 위치

        len = snprintf(p, rem, 
            "{\"id\":%d, \"title\":\"%s\", \"url\":\"%s\", \"thumbnail\":\"%s\", \"duration\":%d, \"last_pos\":%d}",
            id, title?title:"", path?path:"", thumb?thumb:"", duration, last_pos);
        
        if (len < 0 || (size_t)len >= rem) break;
        p += len; rem -= len;
    }
    if (rem > 2) strcpy(p, "]");
    
    sqlite3_finalize(stmt);
    return json;
}

int db_create_user(const char *username, const char *password) {
    if (!g_db) return -2;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users (username, password) VALUES (?, ?);";

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) != SQLITE_OK) {
        return -2;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) return 0; // 성공
    if (rc == SQLITE_CONSTRAINT) return -1; // 아이디 중복 (UNIQUE 제약조건)
    
    return -2; // 기타 에러
}

static int generate_thumbnail_ffmpeg(const char *video_path, const char *out_thumb_path) {
    char command[512];
    
    // 명령어 조합
    snprintf(command, sizeof(command), FFMPEG_CMD_TEMPLATE, video_path, out_thumb_path);
    
    printf("[FFmpeg] Executing: %s\n", command);
    
    // 외부 명령어 실행 (Blocking)
    int ret = system(command);
    
    if (ret == 0) {
        printf("[FFmpeg] Thumbnail generated successfully: %s\n", out_thumb_path);
        return 0;
    } else {
        fprintf(stderr, "[FFmpeg] Failed to generate thumbnail (code: %d)\n", ret);
        // 썸네일 생성 실패가 서버 죽을 일은 아니므로 에러 로그만 남김
        return -1;
    }
}
