#ifndef HISTORY_HANDLER_H
#define HISTORY_HANDLER_H

#include "app/client_context.h"

/**
 * @brief 시청 기록 저장 API (POST /api/history)
 * 요청 바디: video_id=1&timestamp=120
 * * 1. 세션을 통해 user_id를 식별합니다 (보안 필수).
 * 2. 바디에서 video_id와 timestamp를 파싱합니다.
 * 3. db_update_history()를 호출하여 저장합니다.
 */
void handle_api_history(ClientContext *ctx);

#endif