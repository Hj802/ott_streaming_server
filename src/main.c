#define _POSIX_C_SOURCE 200809L

// core Headers
#include "core/reactor.h"
#include "core/thread_pool.h"
#include "core/config_loader.h"

// app Headers
#include "app/http_handler.h"
#include "app/db_handler.h"

 #include <stdio.h>
 #include <stdlib.h>

 int main() {
    ServerConfig config;

    printf("Loading configuration...\n");
    if (load_config("config/server.conf", &config) == -1) {
        fprintf(stderr, "Failed to load config.\n");
        return -1;
    }

    ThreadPool pool = {0};

    int num_thread = config.thread_num;
    int queue_capacity = config.queue_capacity;

    thread_pool_init(&pool, num_thread, queue_capacity);

    Reactor reactor = {0};

    reactor_init(&reactor, &pool, &config);
    reactor_run(&reactor);
 }