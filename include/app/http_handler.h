#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

typedef struct ClientContext ClientContext;

/**
 * @brief 워커 스레드가 수행할 HTTP 요청 처리 진입점
 * * 1. TaskQueue에서 꺼낸 작업을 처리하는 메인 함수.
 * 2. 인자로 전달된 Context를 캐스팅하여 사용.
 * 3. 읽기 -> 파싱 -> 라우팅 -> 응답의 흐름을 지휘.
 * * @param arg (ClientContext*) 타입으로 캐스팅될 인자
 */
void handle_http_request(ClientContext *ctx);

#endif