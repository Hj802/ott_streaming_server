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
 * @brief 사용자 아이디와 비밀번호를 검증합니다.
 * @param username 클라이언트가 입력한 아이디
 * @param password 클라이언트가 입력한 비밀번호
 * @return 검증 성공 시 user_id (양수), 일치하는 유저 없음 -1, DB 오류 -2
 */
int db_verify_user(const char *username, const char *password);

/**
 * @brief 데이터베이스 연결을 닫고 자원을 해제합니다.
 * 서버 종료(main 종료) 시 호출해야 합니다.
 */
void db_cleanup();

/**
 * @brief 시청 이력을 저장하거나 업데이트합니다.
 * @param user_id 사용자 ID
 * @param video_id 비디오 ID
 * @param timestamp 시청 위치 (초 단위)
 */
int db_update_history(int user_id, int video_id, int timestamp);

/**
 * @brief 로그인한 유저의 시청 기록이 포함된 비디오 목록 JSON을 반환합니다.
 * @param user_id 로그인한 사용자 ID
 * @return JSON 문자열 (반드시 free 해야 함)
 */
char* db_get_video_list_json(int user_id);

// 회원가입: 성공 시 0, 중복 아이디면 -1, DB 에러 -2
int db_create_user(const char *username, const char *password);

#endif