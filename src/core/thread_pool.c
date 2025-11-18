/*
# 역할: 미리 만들어진 일꾼(스레드)들을 관리하는 소장.

# 핵심 기능:
- thread_pool_init(n): pthread_create로 n개의 스레드를 생성하고 대기시킴.
- worker_thread_function(): 각 스레드가 실행하는 함수. 
  "작업 큐에 일감 있나?" 확인하고 있으면 꺼내서 실행, 없으면 잠듦(cond_wait).
- thread_pool_shutdown(): 서버 종료 시 스레드 정리.
*/