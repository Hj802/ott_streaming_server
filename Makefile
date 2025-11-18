# =========================================================================
# I. 컴파일러 및 기본 설정 (Configuration)
# =========================================================================

# 컴파일러
CC := gcc

# 링커 (CC와 동일하게 사용)
LD := $(CC)

# 빌드 타겟 이름 (build/ott_server)
TARGET_NAME := ott_server

# 디버그 모드 (1 = -g, 0 = -O2)
DEBUG ?= 1


# =========================================================================
# II. 디렉토리 및 경로 (Directories & Paths)
# =========================================================================

# 빌드 산출물 디렉토리
BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/obj

# 최종 실행 파일 경로
TARGET := $(BIN_DIR)/$(TARGET_NAME)

# 소스 및 헤더 디렉토리
SRC_DIR       := src
INCLUDE_DIR   := include
THIRD_PARTY_DIR := third_party


# =========================================================================
# III. 소스 파일 자동 탐색 (Source File Auto-Discovery)
# =========================================================================
# 
# [핵심 1] 'find' 명령으로 src/ 하위의 모든 .c 파일을 자동으로 찾는다.
# (src/core, src/app, src/main.c 등등)
#
SRCS := $(shell find $(SRC_DIR) -name '*.c')

# 
# [핵심 2] .c 파일 목록을 .o 파일 목록으로 자동 변환한다.
# 예: src/core/reactor.c  ->  build/obj/core/reactor.o
#
OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))


# =========================================================================
# IV. 컴파일 및 링크 플래그 (Flags)
# =========================================================================

# 헤더 검색 경로
# -Iinclude (core/, app/ 헤더를 #include "core/reactor.h" 처럼 쓰기 위함)
# -Ithird_party/include (외부 라이브러리 헤더)
INCLUDE_PATHS := -I$(INCLUDE_DIR) -I$(THIRD_PARTY_DIR)/include
CFLAGS        := -Wall -Wextra $(INCLUDE_PATHS)

# 디버그/릴리즈 플래그 설정
ifeq ($(DEBUG), 1)
    CFLAGS += -g -D_DEBUG
else
    CFLAGS += -O2
endif

# 링커 플래그 (라이브러리 경로)
LDFLAGS := -L$(THIRD_PARTY_DIR)/lib

# 링크할 라이브러리 목록
# R2 (FFmpeg) -> avcodec, avformat, avutil 등
# R9 (멀티스레딩) -> pthread
# R7 (DB) -> sqlite3 (예시)
LDLIBS := -lpthread -lsqlite3 -lavformat -lavcodec -lavutil


# =========================================================================
# V. 빌드 규칙 (Targets & Rules)
# =========================================================================

# 기본 타겟: 'make' 입력 시 실행
.PHONY: all
all: $(TARGET)

# [핵심 3] 최종 실행 파일 (링킹)
# $(OBJS) (.o 파일들)을 링크해서 $(TARGET) (실행 파일)을 생성
$(TARGET): $(OBJS)
	@echo "LD   ==>" $@
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# [핵심 4] 패턴 규칙 (Pattern Rule)
# 'build/obj/core/reactor.o' 같은 타겟을 'src/core/reactor.c'로부터 생성
#
# $@ : 타겟 (build/obj/core/reactor.o)
# $< : 첫 번째 의존성 (src/core/reactor.c)
#
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "CC   ==>" $<
	@mkdir -p $(dir $@)  # [중요] build/obj/core 같은 하위 디렉토리를 생성
	$(CC) $(CFLAGS) -c $< -o $@

# 'make clean' 실행 시
.PHONY: clean
clean:
	@echo "CLEAN==>" $(BUILD_DIR)
	@rm -rf $(BUILD_DIR)

# 'make run' 실행 시 (간단한 테스트용)
.PHONY: run
run: all
	@echo "RUN  ==>" $(TARGET)
	@./$(TARGET)