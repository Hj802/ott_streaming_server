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

    printf("[Response] Sent Error %d to %s\n", status_code, ctx->client_ip);

    // 소켓 닫기 및 메모리 해제
    close(ctx->client_fd);
    free(ctx);
}