#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

typedef struct ClientContext ClientContext;

typedef enum {
    // 에러 코드 (음수)

    // 4XX: 클라이언트 오류
    ERR_BAD_REQUEST = -400,       
    ERR_FORBIDDEN = -403,             
    ERR_NOT_FOUND = -404,              
    ERR_RANGE_NOT_SATISFIABLE = -416,  

    // 5XX: 서버 오류
    ERR_INTERNAL_SERVER = -500,       
    ERR_SERVICE_UNAVAILABLE = -503,    

    // 정상
    RESULT_OK = 0
} HttpResult;

/**
 * @brief 클라이언트에게 HTTP 에러 응답을 전송하고 연결을 종료합니다.
 * * [주의] 이 함수는 내부적으로 close(fd)와 free(ctx)를 수행합니다.
 * 따라서 이 함수를 호출한 직후에는 절대 ctx에 접근하지 말고 즉시 return 해야 합니다.
 * * @param ctx 클라이언트 컨텍스트
 * @param status_code HTTP 상태 코드 (404, 403, 416, 500 등)
 */
void send_error_response(ClientContext *ctx, int status_code);

/**
 * @brief key=value&key2=value2 형태의 문자열을 파싱하여 특정 키의 값을 찾습니다.
 * @param body 원본 데이터 포인터
 * @param key 찾고자 하는 키 이름 (예: "id")
 * @param out_buf 값을 저장할 버퍼
 * @return 찾으면 0, 못 찾으면 -1
 */
int http_get_form_param(const char *body, const char *key, char *out_buf, size_t out_len);

/**
 * @brief 소켓 버퍼 상태와 관계없이 모든 데이터를 보낼 때까지 반복합니다 (Blocking).
 * @param fd 대상 소켓 파일 디스크립터
 * @param data 보낼 데이터 버퍼
 * @param len 보낼 데이터 총 길이
 * @return 성공 0, 실패 -1
 */
int send_all_blocking(int fd, const char *data, size_t len);

#endif