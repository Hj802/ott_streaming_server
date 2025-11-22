# =========================================================================
# I. 컴파일러 및 기본 설정
# =========================================================================
CC := gcc
LD := $(CC)
TARGET_NAME := ott_server
DEBUG ?= 1

# =========================================================================
# II. 디렉토리 및 경로
# =========================================================================
BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/obj

TARGET := $(BIN_DIR)/$(TARGET_NAME)

SRC_DIR         := src
INCLUDE_DIR     := include
THIRD_PARTY_DIR := third_party

# =========================================================================
# III. 소스 및 의존성 파일 자동 탐색
# =========================================================================
SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# [추가] .o 파일에 대응하는 .d (의존성) 파일 목록 생성
DEPS := $(OBJS:.o=.d)

# =========================================================================
# IV. 컴파일 및 링크 플래그
# =========================================================================
# [확인] -Iinclude 설정으로 인해 #include "core/reactor.h" 사용 가능
INCLUDE_PATHS := -I$(INCLUDE_DIR) -I$(THIRD_PARTY_DIR)/include

# [추가] -MMD -MP : 헤더 파일 의존성 정보를 자동으로 생성 (.d 파일)
CFLAGS := -Wall -Wextra $(INCLUDE_PATHS) -MMD -MP

ifeq ($(DEBUG), 1)
    CFLAGS += -g -D_DEBUG
else
    CFLAGS += -O2
endif

LDFLAGS := -L$(THIRD_PARTY_DIR)/lib
LDLIBS  := -lpthread -lsqlite3 -lavformat -lavcodec -lavutil

# =========================================================================
# V. 빌드 규칙
# =========================================================================
.PHONY: all clean run

all: $(TARGET)

# 1. 링킹 (Linking)
$(TARGET): $(OBJS)
	@echo "LD   ==> $@"
	@mkdir -p $(BIN_DIR)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# 2. 컴파일 (Compilation)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "CC   ==> $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# [추가] 생성된 의존성 파일(.d)들을 포함시킴
# 헤더 파일이 변경되면 관련된 .o 파일들을 다시 컴파일하도록 함
-include $(DEPS)

clean:
	@echo "CLEAN==> $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)

run: all
	@echo "RUN  ==> $(TARGET)"
	@./$(TARGET)