#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include "core/config_loader.h"

// 데이터 타입을 구분하기 위한 열거형
typedef enum {
    TYPE_INT,
    TYPE_STRING
} ConfigType;

// 매핑 테이블을 위한 구조체 정의
typedef struct {
    const char *key_name;   // 설정 파일에 적힌 키 이름 (예: "PORT")
    ConfigType type;        // 데이터 타입 (INT냐 STRING이냐)
    size_t offset;          // 구조체 시작 주소로부터의 거리 (byte 단위)
    size_t max_len;         // 문자열 최대 길이 (int는 0으로)
} ConfigMapping;

// 테이블 정의
static ConfigMapping config_map[] = {
    {"PORT",                TYPE_INT,   offsetof(ServerConfig, port),       0},
    {"MAX_CLIENTS",         TYPE_INT,   offsetof(ServerConfig, max_clients),   0},
    {"TIMEOUT_SEC",         TYPE_INT,   offsetof(ServerConfig, timeout_sec),   0},
    {"LOG_LEVEL",           TYPE_INT,   offsetof(ServerConfig, log_level),     0},
    {"QUEUE_CAPACITY",      TYPE_INT,   offsetof(ServerConfig, queue_capacity), 0},
    {"WORKER_THREAD_COUNT", TYPE_INT,   offsetof(ServerConfig, thread_num), 0},
    {"HOST",                TYPE_STRING,offsetof(ServerConfig, server_host),   MAX_HOST_LEN},
    {NULL, 0, 0} // 배열의 끝
};

static char* trim_whitespace(char *str) {
    char *end;

    // 앞쪽 공백 스킵
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0) return str; // 문자열이 공백뿐인 경우

    // 뒤쪽 공백 제거
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Null terminate
    *(end + 1) = 0;

    return str;
}

int load_config(const char *filename, ServerConfig *config) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("Config file open failed");
        return -1;
    }

    // 기본값 설정
    config->port = 8080;
    config->max_clients = 1000;
    config->timeout_sec = 30;
    config->log_level = 1;
    config->queue_capacity = 1000;
    config->thread_num = 10;
    strncpy(config->server_host, "localhost", MAX_HOST_LEN - 1);

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *key, *value, *saveptr; // saveptr for strtok_r

        // 주석 처리
        char *comment_pos = strchr(line, '#');
        if (comment_pos) *comment_pos = '\0';

        // 앞뒤 공백 제거 후 빈 줄이면 스킵
        char *trimmed_line = trim_whitespace(line);
        if (strlen(trimmed_line) == 0) continue;

        key = strtok_r(trimmed_line, "=", &saveptr);
        value = strtok_r(NULL, "=", &saveptr);

        if (key && value) {
            key = trim_whitespace(key);  
            value = trim_whitespace(value);
            
            int found = 0;
            // 테이블을 순회하며 매칭되는 키를 찾는다.
            for (int i = 0; config_map[i].key_name != NULL; i++) {
                if (strcmp(key, config_map[i].key_name) == 0) {
                    
                    // 포인터 연산으로 해당 멤버의 주소를 계산.
                    // (char*)로 캐스팅해야 바이트 단위로 주소 이동이 가능.
                    void *target_addr = (char*)config + config_map[i].offset;

                    if (config_map[i].type == TYPE_INT) {
                        // 해당 주소를 int 포인터로 변환하여 값 대입
                        char* end_ptr;
                        *(int*)target_addr = (int)strtol(value, &end_ptr, 10); 
                        if(end_ptr == value || *end_ptr!= '\0'){
                            fprintf(stderr, "[Config Error] Invalid integer for '%s' (value: '%s')\n", key, value);
                            fclose(fp);
                            return -1;
                        }
                    } else if (config_map[i].type == TYPE_STRING) {
                        // 해당 주소를 char 배열로 보고 문자열 복사
                        size_t len = config_map[i].max_len;
                        strncpy((char*)target_addr, value, len - 1);
                        ((char*)target_addr)[len - 1] = '\0';
                    }
                    found = 1;
                    break; // 찾았으면 루프 탈출
                }
            }
            
            if (!found) {
                printf("Warning: Unknown config key '%s'\n", key);
            }
        } // if (key && value)
    } // while (fgets(line, sizeof(line), fp))
    
    fclose(fp);
    return 0;
}