#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "net.h"
#include "protocol.h"
#include "lobby.h"
#include "client.h"
#include "config.h"

#define MAX_CLIENTS 128
#define MAX_ROOMS 64
#define LINE_MAX 1024
#define CLIENT_IDLE_TIMEOUT_SEC 15

static Client g_clients[MAX_CLIENTS];       // Global array of all client slots
static int g_limit_clients = MAX_CLIENTS;   // Runtime limit for how many client slots are used

static volatile sig_atomic_t g_running = 1; // Main loop running flag

/**
 * @brief Signal handler for graceful shutdown
 *
 * Handles SIGINT/SIGTERM and requests main loop termination
 *
 * @param sig   Signal number
 */
static void on_signal_stop(int sig) {
    (void)sig;
    g_running = 0;
}

/**
 * @brief Allocates a free client slot and initializes it for a new connection
 *
 * Uses g_limit_clients as the runtime maximum. The client is marked as connected+online, fd is set, room_id is initialized to -1, and last_seen is updated
 *
 * @param fd    Accepted client socket file descriptor
 *
 * @return Index of allocated client slot, or -1 if no free slot exists
 */
static int alloc_client(int fd) {
    for (int i = 0; i < g_limit_clients; i++) {
        if (g_clients[i].slot == C_EMPTY) {
            memset(&g_clients[i], 0, sizeof(g_clients[i]));
            g_clients[i].slot = C_CONNECTED;
            g_clients[i].fd = fd;
            g_clients[i].room_id = -1;
            g_clients[i].last_seen = time(NULL);
            g_clients[i].online = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief Drops a client connection (disconnect handling)
 *
 * Marks the client as offline and informs lobby layer
 *
 * @param idx   Client slot index
 */
static void drop_client(int idx) {
    if (idx < 0) {
        return;
    }
    if (g_clients[idx].slot == C_EMPTY) {
        return;
    }

    lobby_on_disconnect(idx);

    if (g_clients[idx].fd >= 0) {
        close(g_clients[idx].fd);
    }

    g_clients[idx].fd = -1;
    g_clients[idx].online = 0;
    g_clients[idx].last_seen = time(NULL);
}

/**
 * @brief Periodically drops idle clients based on the last received activity timestamp
 */
static void keepalive_tick(void) {
    time_t now = time(NULL);

    for (int i = 0; i < g_limit_clients; i++) {
        if (g_clients[i].slot == C_EMPTY) {
            continue;
        } 
        if (!g_clients[i].online) {
            continue;
        }
        if (g_clients[i].fd < 0) {
            continue;
        }

        if ((int)(now - g_clients[i].last_seen) > CLIENT_IDLE_TIMEOUT_SEC) {
            drop_client(i);
        }
    }
}

/**
 * @brief Sends a single protocol line to a client if they are online
 *
 * No-op if the slot is empty or the client is offline
 *
 * @param idx   Client slot index
 * @param line  Text line ending with '\n'
 */
static void send_line(int idx, const char* line) {
    if (g_clients[idx].slot == C_EMPTY) {
        return;
    }
    if (g_clients[idx].fd < 0) {
        return;
    }
    net_send_all(g_clients[idx].fd, line, strlen(line));
}


/**
 * @brief Sends an error response in protocol format
 *
 * Format: "ERR <cmd> code=<code> msg=<msg>\n"
 *
 * @param idx   Client slot index
 * @param cmd   Command name that caused the error (or "?" when unknown)
 * @param code  Error code token
 * @param msg   Error message token
 */
static void send_err(int idx, const char* cmd, const char* code, const char* msg) {
    char out[LINE_MAX];
    snprintf(out, sizeof(out), "ERR %s code=%s msg=%s\n", cmd, code, msg);
    send_line(idx, out);
}


/**
 * @brief Dispatches a parsed request message to the lobby/game handlers
 *
 * Validates required fields for some commands before calling lobby handlers
 *
 * @param idx   Client slot index
 * @param m     Parsed protocol message (must be PT_REQ)
 */
static void handle_req(int idx, const ProtoMsg* m) {
    if (strcmp(m->cmd, "LOGIN") == 0) {
        const char* nick = proto_get(m, "nick");
        if (!nick) { 
            send_err(idx, "LOGIN", "BAD_FORMAT", "missing_nick"); 
            return; 
        }
        lobby_handle_login(idx, nick);

        return;
    }
    if (strcmp(m->cmd, "RESUME") == 0) {
        const char* nick = proto_get(m, "nick");
        const char* ses  = proto_get(m, "session");
        if (!nick || !ses) {
            send_err(idx, "RESUME", "BAD_FORMAT", "missing_fields");
            return;
        }
        lobby_handle_resume(idx, nick, ses);

        return;
    }
    if (strcmp(m->cmd, "LIST_ROOMS") == 0) {
        lobby_handle_list_rooms(idx);
        return;
    }
    if (strcmp(m->cmd, "CREATE_ROOM") == 0) {
        const char* name = proto_get(m, "name");
        const char* size = proto_get(m, "size");
        if (!name || !size) {
            send_err(idx, "CREATE_ROOM", "BAD_FORMAT", "missing_fields");
            return;
        }
        lobby_handle_create_room(idx, name, atoi(size));

        return;
    }
    if (strcmp(m->cmd, "JOIN_ROOM") == 0) {
        const char* room = proto_get(m, "room");
        if (!room) {
            send_err(idx, "JOIN_ROOM", "BAD_FORMAT", "missing_room");
            return;
        }
        lobby_handle_join_room(idx, atoi(room));
        
        return;
    }
    if (strcmp(m->cmd, "LEAVE_ROOM") == 0) {
        lobby_handle_leave_room(idx);
        return;
    }
    if (strcmp(m->cmd, "START_GAME") == 0) {
        lobby_handle_start_game(idx);
        return;
    }
    if (strcmp(m->cmd, "PLAY") == 0) {
        lobby_handle_play(idx, m);
        return;
    }
    if (strcmp(m->cmd, "DRAW") == 0) {
        lobby_handle_draw(idx);
        return;
    }
    if (strcmp(m->cmd, "LOGOUT") == 0) {
        lobby_handle_logout(idx);
        return;
    }
    if (strcmp(m->cmd, "PING") == 0) {
        Client* c = &g_clients[idx];
        c->online = 1;
        c->last_seen = time(NULL);
        send_line(idx, "RESP PONG\n");

        return;
    }

    send_err(idx, m->cmd, "UNKNOWN_CMD", "unknown");
}

/**
 * @brief Parses one complete line and processes it as a request
 *
 * Invalid protocol increments strikes, returns BAD_FORMAT, and disconnects after 3 strikes
 *
 * @param idx   Client slot index
 * @param line  Null-terminated line
 */
static void process_line(int idx, const char* line) {
    ProtoMsg m;
    ProtoResult r = proto_parse(line, &m);
    if (r != PROTO_OK) {
        g_clients[idx].strikes++;
        send_err(idx, "?", "BAD_FORMAT", "parse_error");
        if (g_clients[idx].strikes >= 3) {
            drop_client(idx);
        }

        return;
    }
    if (m.type != PT_REQ) {
        send_err(idx, m.cmd, "BAD_FORMAT", "expected_req");
        return;
    }
    handle_req(idx, &m);
}

/**
 * @brief Reads incoming data from a non-blocking socket and processes full lines
 *
 * Buffers partial reads. Splits by '\n'. Each complete line is validated for maximum length and then passed to process_line()
 *
 * @param idx   Client slot index
 */
static void on_readable(int idx) {
    Client* c = &g_clients[idx];
    char tmp[2048];

    for (;;) {
        ssize_t n = recv(c->fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            c->last_seen = time(NULL);
            if (c->rlen + (size_t)n > sizeof(c->rbuf)) {
                send_err(idx, "?", "BAD_FORMAT", "buffer_overflow");
                drop_client(idx);

                return;
            }
            memcpy(c->rbuf + c->rlen, tmp, (size_t)n);
            c->rlen += (size_t)n;

            size_t start = 0;
            for (size_t i = 0; i < c->rlen; i++) {
                if (c->rbuf[i] == '\n') {
                    size_t len = i - start + 1;
                    if (len >= LINE_MAX) {
                        send_err(idx, "?", "BAD_FORMAT", "line_too_long");
                        drop_client(idx);

                        return;
                    }
                    char line[LINE_MAX];
                    memcpy(line, c->rbuf + start, len);
                    line[len] = '\0';

                    for (size_t k = 0; k < len; k++) {
                        if (line[k] == '\r' || line[k] == '\n') {
                            line[k] = '\0';
                        }
                    }

                    if (line[0] != '\0') {
                        process_line(idx, line);
                    }
                    start = i + 1;
                }
            }
            if (start > 0) {
                memmove(c->rbuf, c->rbuf + start, c->rlen - start);
                c->rlen -= start;
            }
            continue;
        }

        if (n == 0) {
            drop_client(idx);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }

        drop_client(idx);

        return;
    }
}

/**
 * @brief Prints server usage/help text
 *
 * @param prog  Program name
 */
static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [-c server.ini] [--ip X] [--port N] [--max-clients N] [--max-rooms N]\n"
        "Notes:\n"
        "\tclient limit = %d\n"
        "\troom limit = %d\n"
        "Stop:\n"
        "\tType 'quit' or 'exit'\n",
        prog, MAX_CLIENTS, MAX_ROOMS
    );
}

/**
 * @brief Reads stdin command and stops the server
 *
 * Called only when poll() indicates stdin is readable
 * If stdin is closed, server is stopped as well
 */
static void handle_stdin_quit(void) {
    char buf[256];
    if (!fgets(buf, (int)sizeof(buf), stdin)) {
        g_running = 0;
        return;
    }

    for (size_t i = 0; buf[i]; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') {
            buf[i] = '\0';
            break;
        }
    }

    if (strcmp(buf, "quit") == 0 || strcmp(buf, "exit") == 0 || strcmp(buf, "q") == 0) {
        g_running = 0;
    }
}

/**
 * @brief Server entry point
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 *
 * @return 0 on clean exit, non-zero on startup/validation failure
 */
int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);

    signal(SIGINT, on_signal_stop);
    signal(SIGTERM, on_signal_stop);

    ServerConfig cfg;
    config_defaults(&cfg);

    const char* config_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) { 
                usage(argv[0]);
                return 2;
            }
            config_path = argv[i + 1];
            i++;
        }
    }

    if (config_path) {
        if (!config_load_file(&cfg, config_path)) {
            fprintf(stderr, "Warning: cannot load config file '%s', using defaults\n", config_path);
        }
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            i++;
            continue;
        }

        if (strcmp(argv[i], "--ip") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            snprintf(cfg.ip, sizeof(cfg.ip), "%s", argv[++i]);

            continue;
        }
        if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            cfg.port = atoi(argv[++i]);

            continue;
        }
        if (strcmp(argv[i], "--max-clients") == 0 || strcmp(argv[i], "--max_clients") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            cfg.max_clients = atoi(argv[++i]);

            continue;
        }
        if (strcmp(argv[i], "--max-rooms") == 0 || strcmp(argv[i], "--max_rooms") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            cfg.max_rooms = atoi(argv[++i]);

            continue;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    if (cfg.port < 1 || cfg.port > 65535) {
        fprintf(stderr, "Error: invalid port (%d)\n", cfg.port);
        return 2;
    }
    if (cfg.max_clients < 1) {
        fprintf(stderr, "Error: invalid max_clients %d\n", cfg.max_clients);
        return 2;
    }
    if (cfg.max_rooms < 1) {
        fprintf(stderr, "Error: invalid max_rooms %d\n", cfg.max_rooms);
        return 2;
    }

    if (cfg.max_clients > MAX_CLIENTS) cfg.max_clients = MAX_CLIENTS;
    if (cfg.max_rooms > MAX_ROOMS) cfg.max_rooms = MAX_ROOMS;

    g_limit_clients = cfg.max_clients;

    config_print(&cfg);

    lobby_init(send_line, send_err, g_clients, cfg.max_clients, cfg.max_rooms);

    int lfd = net_listen(cfg.ip, cfg.port);
    if (lfd < 0) {
        fprintf(stderr, "Listen failed\n");
        return 1;
    }
    printf("Listening on %s:%d\n", cfg.ip, cfg.port);
    printf("Type 'quit' or 'exit' to stop\n");

    struct pollfd pfds[MAX_CLIENTS + 2];
    int map[MAX_CLIENTS + 2];

    while (g_running) {
        int nfd = 0;

        pfds[nfd].fd = 0;
        pfds[nfd].events = POLLIN;
        map[nfd] = -2;
        nfd++;

        pfds[nfd].fd = lfd;
        pfds[nfd].events = POLLIN;
        map[nfd] = -1;
        nfd++;

        for (int i = 0; i < g_limit_clients; i++) {
            if (g_clients[i].slot != C_EMPTY && g_clients[i].fd >= 0) {
                pfds[nfd].fd = g_clients[i].fd;
                pfds[nfd].events = POLLIN;
                map[nfd] = i;
                nfd++;
            }
        }

        int rc = poll(pfds, nfd, 250);
        if (rc < 0) {
            continue;
        }

        if (pfds[0].revents & POLLIN) {
            handle_stdin_quit();
        }

        if (pfds[1].revents & POLLIN) {
            for (;;) {
                int cfd = accept(lfd, NULL, NULL);
                if (cfd < 0) {
                    break;
                }
                net_set_nonblock(cfd);
                int idx = alloc_client(cfd);
                if (idx < 0) {
                    close(cfd);
                }
                else {
                    send_line(idx, "EVT SERVER msg=welcome\n");
                }
            }
        }

        for (int p = 2; p < nfd; p++) {
            int idx = map[p];
            if (idx < 0) {
                continue;
            }

            if (pfds[p].revents & POLLIN) {
                on_readable(idx);
            }
            if (pfds[p].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                drop_client(idx);
            }
        }

        lobby_tick();
        keepalive_tick();
    }

    printf("Shutting down...\n");

    for (int i = 0; i < g_limit_clients; i++) {
        if (g_clients[i].slot != C_EMPTY && g_clients[i].fd >= 0) {
            close(g_clients[i].fd);
            g_clients[i].fd = -1;
        }
    }

    if (lfd >= 0) {
        close(lfd);
    }

    return 0;
}
