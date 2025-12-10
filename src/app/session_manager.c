#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include "app/session_manager.h"

typedef struct SessionNode {
    char session_id[SESSION_ID_LENGTH]; // Key (32 bytes + NULL)
    int user_id;                         // Value
    time_t last_accessed;               // Expiry Check용
    struct SessionNode *next;           // Collision 처리용 (Chaining)
} SessionNode;

typedef struct {
    SessionNode **buckets;              // 노드 포인터 배열 (Bucket)
    int bucket_count;                   // 버킷 크기
    pthread_mutex_t mutex;              // Thread-safe 확보
} SessionTable;

// 내부 전역 변수
static SessionTable *g_session_table = NULL;
static const int HASH_TABLE_SIZE = 1024; // 버킷 수

// 내부 헬퍼 함수
static unsigned long hash_djb2(const char *str); // DJB2 해시 함수: 문자열을 인덱스 정수로 변환
static void generate_session_id(char *buf, size_t len); // 무작위 세션 ID 생성

int session_system_init(void) {
    // 테이블 본체 할당
    g_session_table = (SessionTable *)malloc(sizeof(SessionTable));
    if (!g_session_table) return -1;

    // 버킷 배열 할당 (전부 NULL로 초기화)
    g_session_table->buckets = (SessionNode **)calloc(HASH_TABLE_SIZE, sizeof(SessionNode *));
    if (!g_session_table->buckets) {
        free(g_session_table);
        return -1;
    }

    g_session_table->bucket_count = HASH_TABLE_SIZE;

    // Mutex 초기화
    if (pthread_mutex_init(&g_session_table->mutex, NULL) != 0) {
        free(g_session_table->buckets);
        free(g_session_table);
        return -1;
    }

    // 난수 생성을 위한 시드 초기화 (세션 ID 생성용)
    srand((unsigned int)time(NULL));

    printf("[Session] System initialized with %d buckets.\n", HASH_TABLE_SIZE);
    return 0;
}

static unsigned long hash_djb2(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    return hash;
}
static void generate_session_id(char *buf, size_t len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < len - 1; i++) {
        int key = rand() % (int)(sizeof(charset) - 1);
        buf[i] = charset[key];
    }
    buf[len - 1] = '\0';
}

int session_create(int user_id, char *out_buf, size_t buf_len) {
    if (!g_session_table || buf_len < SESSION_ID_LENGTH) return -1;

    // 세션 노드 생성 및 초기화
    SessionNode *new_node = (SessionNode *)malloc(sizeof(SessionNode));
    if (!new_node) return -1;

    generate_session_id(new_node->session_id, SESSION_ID_LENGTH);
    new_node->user_id = user_id;
    new_node->last_accessed = time(NULL);
    new_node->next = NULL;

    // 해시 계산
    unsigned long h = hash_djb2(new_node->session_id);
    int bucket_idx = h % g_session_table->bucket_count;

    // 임계 영역 (Critical Section): 해시 테이블 조작
    pthread_mutex_lock(&g_session_table->mutex);
    
    // 리스트 맨 앞에 삽입 (O(1))
    new_node->next = g_session_table->buckets[bucket_idx];
    g_session_table->buckets[bucket_idx] = new_node;

    pthread_mutex_unlock(&g_session_table->mutex);

    // 생성된 ID 반환
    strncpy(out_buf, new_node->session_id, buf_len);
    
    printf("[Session] Created: ID=%s for User=%d at Bucket[%d]\n", 
            new_node->session_id, user_id, bucket_idx);
            
    return 0;
}

int session_get_user(const char *session_id) {
    if (!g_session_table || !session_id) return -1;

    // 1. 해시 계산 및 버킷 특정
    unsigned long h = hash_djb2(session_id);
    int bucket_idx = h % g_session_table->bucket_count;
    
    time_t now = time(NULL);
    int found_user_id = -1;

    // 2. 임계 영역 시작
    pthread_mutex_lock(&g_session_table->mutex);

    SessionNode *curr = g_session_table->buckets[bucket_idx];
    SessionNode *prev = NULL;

    while (curr) {
        if (strcmp(curr->session_id, session_id) == 0) {
            // [검증] 만료 여부 확인
            if (now - curr->last_accessed > SESSION_TTL_SEC) {
                // 만료됨 -> 리스트에서 연결 끊기 및 메모리 해제
                if (prev) prev->next = curr->next;
                else g_session_table->buckets[bucket_idx] = curr->next;

                free(curr);
                printf("[Session] Expired and removed: %s\n", session_id);
                // found_user_id는 -1 유지
            } else {
                // 유효함 -> 마지막 접근 시간 갱신 (Sliding Window)
                curr->last_accessed = now;
                found_user_id = curr->user_id;
            }
            break; // 찾았으므로 루프 탈출
        }
        prev = curr;
        curr = curr->next;
    }

    pthread_mutex_unlock(&g_session_table->mutex);
    return found_user_id;
}

void session_remove(const char *session_id) {

}

void session_system_cleanup(void) {

}