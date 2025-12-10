#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#include "app/http_utils.h"
#include "app/client_context.h"

static const char* get_status_text(int code) {
    switch (code) {
        case ERR_BAD_REQUEST:           return "Bad Request";
        case ERR_FORBIDDEN:             return "Forbidden";
        case ERR_NOT_FOUND:             return "Not Found";
        case ERR_RANGE_NOT_SATISFIABLE: return "Range Not Satisfiable";
        case ERR_INTERNAL_SERVER:       return "Internal Server Error";
        case ERR_SERVICE_UNAVAILABLE:   return "Service Unavailable";
        default:                        return "Error";
    }
}

void send_error_response(ClientContext *ctx, int status_code) {
    if (!ctx) return;

    status_code = -status_code;
    // [안전장치] 이미 파일 데이터를 보내던 중이라면 에러 헤더를 보낼 수 없음
    // 프로토콜이 깨지므로 그냥 조용히 연결을 끊는 것이 상책
    if (ctx->state == STATE_RES_SENDING_BODY) {
        printf("[Error] Error occurred during streaming. Closing connection.\n");
        if (ctx->file_fd > 0) close(ctx->file_fd);
        close(ctx->client_fd);
        free(ctx);
        return;
    }

    // 에러 응답 생성
    // 버퍼 재사용 (요청 버퍼는 이제 필요 없음)
    const char* status_text = get_status_text(status_code);
    char response[512];
    
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text
    );

    // 전송 시도 (Non-blocking)
    // 에러 메시지는 작으므로 보통 한 번에 전송됨. 
    // 만약 EAGAIN이 뜨더라도 에러 처리를 위해 대기하는 것은 낭비이므로
    // Best-Effort로 시도하고 실패 시 그냥 연결 종료.
    send(ctx->client_fd, response, len, 0);

    // [자원 정리]
    // 스트리밍을 위해 열어둔 파일이 있다면 닫기
    if (ctx->file_fd > 0) {
        close(ctx->file_fd);
    }

    printf("[Response] Sent Error %d to %s.  %s\n", status_code, ctx->client_ip, ctx->request_path);

    // 소켓 닫기 및 메모리 해제
    close(ctx->client_fd);
    free(ctx);
}

int http_get_form_param(const char *body, const char *key, char *out_buf, size_t out_len){
    if (!body || !key || !out_buf || out_len == 0) return -1;

    size_t key_len = strlen(key);
    const char *p = body;

    while ((p = strstr(p, key)) != NULL) {
        // [경계 검사] 키가 문자열의 시작이거나 직전 문자가 '&'여야 함
        // 예: 'id'를 찾을 때 'user_id'에 걸리는 것 방지
        if (p == body || *(p - 1) == '&') {
            const char *val_start = p + key_len;
            
            // 키 뒤에 '='가 오는지 확인
            if (*val_start == '=') {
                val_start++; // '=' 다음 위치(값의 시작)로 이동
                
                // 값의 끝('&' 또는 '\0') 찾기
                const char *val_end = strchr(val_start, '&');
                if (!val_end) val_end = val_start + strlen(val_start);

                // 결과 복사 (out_len 범위 내)
                size_t val_len = (size_t)(val_end - val_start);
                size_t copy_len = (val_len < out_len - 1) ? val_len : out_len - 1;

                memcpy(out_buf, val_start, copy_len);
                out_buf[copy_len] = '\0';

                return 0; // 성공
            }
        }
        // 키는 포함되었으나 경계 검사에 실패한 경우 다음 위치부터 재검색
        p += key_len;
    }

    return -1; // 찾지 못함
}