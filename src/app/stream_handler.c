// R6, R8, R9: range 요청, 스트리밍 처리

/*
# 역할: 동영상 스트리밍의 핵심. 가장 복잡한 곳.

# 핵심 기능:
- handle_streaming(filename, range_header):
- Range: bytes=100- 헤더 파싱.
- http_response_header 생성 (206 Partial Content).
- 파일 전송: sendfile() 시스템 콜을 사용하여 커널이 직접 파일을 소켓으로 쏘게 만듦 (Zero-copy).
- R7(이어보기) 저장을 위해 "현재 몇 초까지 봤는지" 정보를 주기적으로 db_handler에 업데이트 요청.
*/