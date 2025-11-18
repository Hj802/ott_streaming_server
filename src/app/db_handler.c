// R7: 시청 이력 DB 연동

/*
# 역할: 데이터베이스(SQLite)와 대화하는 유일한 창구.

# 핵심 기능:
- db_init(): 테이블 생성 (CREATE TABLE IF NOT EXISTS...).
- db_get_user(id): SELECT 쿼리 실행.
- db_update_history(user_id, video_id, time): 시청 기록 INSERT 혹은 UPDATE.
- 시니어의 팁: SQLite는 기본적으로 파일 락을 사용하네. 여러 워커 스레드가 동시에 쓰려고 하면 에러가 날 수 있으니, 
    DB 전용 뮤텍스를 두거나 설정을 잘 확인해야 하네.
*/