/*
#역할: Event Loop 그 자체. epoll의 Wrapper

# 핵심 기능: 
- reactor_create(): epoll_create1 호출.
- reactor_add(fd, callback): 감시할 소켓(fd)과, 이벤트 발생 시 실행할 함수(callback)를 등록.
- reactor_run(): epoll_wait를 무한 루프(while(1))로 돌면서, 이벤트가 터지면 등록된 callback 함수를 실행.
- 시니어의 팁: 모든 소켓은 Non-blocking 모드로 설정되어야 하네. (fcntl 사용)

- 한순간도 blocking 되면 X
*/