#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "app/http_handler.h"
#include "app/stream_handler.h"
#include "app/client_event_manager.h"
#include "app/client_context.h"


void handle_client_event(void* arg) {
    ClientContext* ctx = (ClientContext*)arg;
    // [Dispatcher 역할]
    switch (ctx->state) {
        case STATE_REQ_RECEIVING:
        case STATE_PROCESSING:
            handle_http_request(ctx); // HTTP 처리기 호출
            break;

        case STATE_RES_SENDING_HEADER:
        case STATE_RES_SENDING_BODY:
            handle_streaming_request(ctx); // 스트리밍 처리기 호출
            break;
            
        case STATE_CLOSED:
            // 정리 로직
            // 이미 닫힌 상태라면 자원 해제 확인 후 리턴
            // 보통은 여기까지 오지 않도록 epoll에서 제거되어야 함
            break;
        default:
            fprintf(stderr, "Unknown State: %d\n", ctx->state);
            break;
    }
}