// http_parser.c의 진화.        파싱 + 요청 처리

/*
# 역할: 들어온 데이터가 HTTP인지 확인하고, 적절한 담당자에게 넘겨주는 '라우터(Router)'.

# 핵심 기능:
- parse_request(buffer): 들어온 문자열을 분석해 GET /video.mp4 HTTP/1.1 같은 정보를 구조체로 만듦.
- route_request(request):
    URL이 /login이면 -> auth_handler 호출.
    URL이 /watch이면 -> stream_handler 호출.
    URL이 /이면 -> index.html 읽어서 전송.
*/