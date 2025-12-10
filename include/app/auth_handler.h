#ifndef AUTH_HANDLER_H
#define AUTH_HANDLER_H

typedef struct ClientContext ClientContext;

/**
 * @brief 로그인 요청(POST /login)을 처리합니다.
 * * 1. HTTP Body에서 username과 password를 파싱합니다.
 * 2. db_verify_user()를 호출하여 자격 증명을 확인합니다.
 * 3. 성공 시 session_create()로 세션을 생성합니다.
 * 4. 성공 응답과 함께 Set-Cookie 헤더를 클라이언트에 전송합니다.
 * * @param ctx 클라이언트 컨텍스트
 */
void handle_login(ClientContext *ctx);

/**
 * @brief 로그아웃 요청(POST /logout)을 처리합니다.
 * * 1. 요청 헤더에서 session_id 쿠키를 추출합니다.
 * 2. session_remove()를 호출하여 메모리에서 세션을 삭제합니다.
 * 3. 쿠키를 만료시키는 응답을 전송합니다.
 * * @param ctx 클라이언트 컨텍스트
 */
void handle_logout(ClientContext *ctx);
#endif