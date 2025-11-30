#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

typedef struct ClientContext ClientContext;

typedef enum {
    RESULT_OK = 0,
    
    // 에러 코드 (음수)
    ERR_INTERNAL_SERVER = -1,   // 500
    ERR_NOT_FOUND = -2,         // 404
    ERR_FORBIDDEN = -3,         // 403
    ERR_RANGE_NOT_SATISFIABLE = -4, // 416
    ERR_BAD_REQUEST = -5        // 400
} HttpResult;

/**
 * @brief 클라이언트에게 HTTP 에러 응답을 전송하고 연결을 종료합니다.
 * * [주의] 이 함수는 내부적으로 close(fd)와 free(ctx)를 수행합니다.
 * 따라서 이 함수를 호출한 직후에는 절대 ctx에 접근하지 말고 즉시 return 해야 합니다.
 * * @param ctx 클라이언트 컨텍스트
 * @param status_code HTTP 상태 코드 (404, 403, 416, 500 등)
 */
void send_error_response(ClientContext *ctx, int status_code);

#endif