#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "core/reactor.h"
#include "core/thread_pool.h"
#include "core/config_loader.h"
#include "app/http_handler.h"
#include "app/db_handler.h"

 // 시그널 핸들러용
Reactor *g_reactor_ptr = NULL;

void signal_handler(int sig) {
    if (sig == SIGINT) {
        printf("\nCaught SIGINT, stopping server...\n");
        if (g_reactor_ptr) {
            reactor_stop(g_reactor_ptr);
        }
    }
}

 int main(int argc, char *argv[]) {
    ServerConfig config;
    signal(SIGPIPE, SIG_IGN); // 클라이언트가 연결을 끊었을 때 서버가 죽는 것을 방지

    printf("Loading configuration...\n");
    if (load_config("config/server.conf", &config) == -1) {
        fprintf(stderr, "Failed to load config.\n");
        return -1;
    }

    if (db_init("ott.db") != 0) {
        fprintf(stderr, "Failed to initialize Database.\n");
        return -1;
    }

    ThreadPool pool = {0};;
    if (thread_pool_init(&pool, config.thread_num, config.queue_capacity)) {
        fprintf(stderr, "Failed to init thread pool.\n");
        db_cleanup();
        return -1;
    }

    Reactor reactor = {0};
    if (reactor_init(&reactor, &pool, &config) != 0) {
        fprintf(stderr, "Failed to init reactor.\n");
        thread_pool_shutdown(&pool);
        thread_pool_wait(&pool);
        thread_pool_cleanup(&pool);
        db_cleanup();
        return 1;
    }

    g_reactor_ptr = &reactor;
    signal(SIGINT, signal_handler);

    printf("Server running on port %d...\n", config.port);
    reactor_run(&reactor);

    printf("Cleaning up resources...\n");
    
    thread_pool_shutdown(&pool);
    thread_pool_wait(&pool);
    thread_pool_cleanup(&pool);
    
    reactor_destroy(&reactor);
    
    db_cleanup();

    printf("Server stopped cleanly.\n");
    return 0;
 }