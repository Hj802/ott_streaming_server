#ifndef STATIC_HANDLER_H
#define STATIC_HANDLER_H

typedef struct ClientContext ClientContext;

/**
 * @brief 정적 파일(HTML, CSS, JS, IMG) 요청 처리 핸들러
 * * 1. 요청된 파일 경로(ctx->request_path)를 엽니다.
 * 2. 파일의 확장자를 확인하여 Content-Type을 결정합니다.
 * 3. 200 OK 헤더와 파일 전체 내용을 전송합니다 (Zero-Copy sendfile).
 * * @param ctx 클라이언트 컨텍스트
 */
void handle_static_request(ClientContext *ctx);

#endif