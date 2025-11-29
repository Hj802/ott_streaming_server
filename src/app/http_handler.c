#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include "app/http_handler.h"
#include "app/stream_handler.h"
#include "app/client_context.h"

enum {
    READ_BLOCK = -2,
    READ_ERR = -1,
    READ_EOF
};

static int try_read_request(ClientContext *ctx);
static void parse_request(ClientContext *ctx);
static void route_request(ClientContext *ctx, char *path, char *method);
static void rearm_epoll(ClientContext *ctx);

void handle_http_request(void *arg) {
    ClientContext *ctx = (ClientContext*)arg;

    int read_status = try_read_request(ctx);
    if (read_status == READ_ERR) return;
    if (read_status == READ_BLOCK) return;

    parse_request(ctx);

}


static int try_read_request(ClientContext *ctx) {
    int remaining = (sizeof(ctx->buffer) - 1) - ctx->buffer_len;

    if (remaining <= 0){
        fprintf(stderr, "Error: Request Header too large (Buffer Full)\n");
        return READ_ERR;
    }

    char *ptr = ctx->buffer + ctx->buffer_len;

    ssize_t received = recv(ctx->client_fd, ptr, remaining, 0);

    if (received > 0){
        ctx->buffer_len += received;
        ctx->buffer[ctx->buffer_len] = '\0';
        return received;
    } 
    else if (received == 0) {
        // FIN
        return READ_EOF;
    } 
    else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 에러 X. 데이터 없음
            return READ_BLOCK;
        }
        perror("recv() failed.");
        return READ_ERR;
    }
}

static void parse_request(ClientContext *ctx) {

}

static void route_request(ClientContext *ctx, char *path, char *method) {

}

static void rearm_epoll(ClientContext *ctx) {

}