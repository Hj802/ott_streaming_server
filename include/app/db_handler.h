#ifndef DB_HANDLER_H
#define DB_HANDLER_H

typedef struct ClientContext ClientContext;

/**
 * @brief 데이터베이스 시스템을 초기화.
 * * 1. SQLite DB 파일을 엽니다 (없으면 생성).
 * 2. 'videos' 테이블이 존재하는지 확인하고 없으면 생성(CREATE TABLE)합니다.
 * 3. 테이블이 비어있다면 테스트용 초기 데이터(Seed Data)를 삽입합니다.
 * * @param db_path 데이터베이스 파일 경로 (예: "./ott.db")
 * @return 성공 시 0, 실패 시 -1
 */
int db_init(const char *db_path);

/**
 * @brief 동영상 목록 API 요청을 처리합니다. (GET /api/videos)
 * * 1. DB에서 id, title, filepath, thumbnail 등을 조회합니다.
 * 2. 조회된 데이터를 JSON 포맷의 문자열로 직렬화(Serialization)합니다.
 * 3. ClientContext 버퍼에 기록하고 응답(200 OK, Application/json)을 전송합니다.
 * * @param ctx 클라이언트 컨텍스트
 */
void handle_api_video_list(ClientContext *ctx);

/**
 * @brief 데이터베이스 연결을 닫고 자원을 해제합니다.
 * 서버 종료(main 종료) 시 호출해야 합니다.
 */
void db_cleanup();

#endif