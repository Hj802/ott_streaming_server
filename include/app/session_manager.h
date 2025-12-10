#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H
#include <stddef.h>
#include <stdbool.h>

// [상수 정의]
// 세션 ID의 길이 (32바이트 + NULL)
#define SESSION_ID_LENGTH 33

// 세션 만료 시간 (30분 = 1800초)
#define SESSION_TTL_SEC 1800

/**
 * @brief 세션 시스템(해시 테이블 및 Mutex)을 초기화합니다.
 * 서버 시작 시 한 번 호출해야 합니다.
 * @return 성공 0, 실패 -1
 */
int session_system_init(void);

/**
 * @brief 새로운 세션을 생성하고 메모리에 저장합니다.
 * * 1. 난수 기반의 유니크한 세션 ID를 생성합니다.
 * 2. 해시 테이블에 {session_id : user_id} 매핑을 저장합니다.
 * 3. 생성된 세션 ID를 out_buf에 복사하여 반환합니다.
 * * @param user_id 로그인에 성공한 사용자 식별자 (DB PK)
 * @param out_buf 생성된 세션 ID가 저장될 버퍼 (최소 33바이트 이상)
 * @param buf_len 버퍼의 크기
 * @return 성공 0, 실패 -1
 */
int session_create(int user_id, char *out_buf, size_t buf_len);

/**
 * @brief 세션 ID로 사용자 정보를 조회합니다. (인증 검사)
 * * 1. 해시 테이블에서 세션 ID를 검색합니다 (O(1)).
 * 2. 세션이 존재하고, 만료되지 않았는지 확인합니다.
 * 3. 유효하다면 user_id를 반환하고, '마지막 접근 시간'을 갱신합니다.
 * * @param session_id 클라이언트 쿠키에서 파싱한 세션 문자열
 * @return 유효한 경우 user_id (양수), 유효하지 않거나 만료된 경우 -1
 */
int session_get_user(const char *session_id);

/**
 * @brief 세션을 삭제합니다. (로그아웃)
 * @param session_id 삭제할 세션 ID
 */
void session_remove(const char *session_id);

/**
 * @brief 시스템 종료 시 자원을 해제합니다.
 * 할당된 모든 노드 메모리와 Mutex를 정리합니다.
 */
void session_system_cleanup(void);

#endif