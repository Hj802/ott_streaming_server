/*
# 역할: 메인 스레드(Reactor)와 워커 스레드 사이의 '우체통'.

# 핵심 기능:
- 자료구조: 연결 리스트(Linked List)나 원형 버퍼로 구현된 큐.
- enqueue(task): 메인 스레드가 작업을 넣음. (반드시 Mutex 락 필요!)
- dequeue(): 워커 스레드가 작업을 꺼냄. (반드시 Mutex 락 필요!)
- 주의: 여기가 **동시성 문제(Race Condition)**가 발생하는 지점. 
pthread_mutex_lock과 pthread_cond_signal을 정확히 사용해야 하네.
*/