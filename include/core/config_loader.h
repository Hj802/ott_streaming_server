#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#define MAX_HOST_LEN 128

// 설정값들을 저장할 구조체
typedef struct ServerConfig{
    int port;
    int max_clients;
    int timeout_sec;
    int log_level;
    int queue_capacity;
    int thread_num;
    char server_host[MAX_HOST_LEN]; // 문자열 설정 예시 추가
} ServerConfig;

/**
 * 설정 파일을 읽어서 config 구조체에 값을 채워주는 함수
 * @param filename: 설정 파일 경로 (예: "server.conf")
 * @param config: 값을 채울 구조체의 포인터
 * @return: 성공 시 0, 실패 시 -1
 */
int load_config(const char* filename, ServerConfig* config);

#endif