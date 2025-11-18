// 지휘자

/*
# 역할: 프로그램 진입점.

# 흐름:
- config 로드.
- thread_pool_init() (일꾼들 출근).
- db_init() (장부 준비).
- reactor_create() (전화기 설치).
- 서버 소켓 생성 (socket, bind, listen) 후 reactor_add에 등록.
- reactor_run() (영업 시작 - 무한 대기).
*/