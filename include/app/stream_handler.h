#ifndef STREAM_HANDLER_H
#define STREAM_HANDLER_H

typedef ClientContext ClientContext;

/**
 * @brief 비디오 스트리밍 요청을 처리하는 핵심 함수
 * * 이 함수는 두 가지 상황에서 호출:
 * 1. [Initial]: http_handler에서 라우팅되어 처음 호출될 때 (파일 열기, 헤더 전송)
 * 2. [Resume]: 파일 전송 중 소켓 버퍼가 차서(EAGAIN) 중단되었다가, 
 * 다시 쓰기 가능(EPOLLOUT)해져서 호출될 때 (sendfile 재개)
 * * @param ctx 클라이언트 문맥 (파일 FD, 오프셋, 소켓 FD 포함)
 */
void handle_streaming_request(ClientContext* ctx);

#endif