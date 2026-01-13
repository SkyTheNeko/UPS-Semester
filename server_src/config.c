#include "config.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/**
 * @brief Trims leading and trailing whitespace in-place
 *
 * Removes whitespace characters from the beginning and the end of the given string
 *
 * @param s     The string to be trimmed
 */
static void trim(char* s) {
    if (!s) {
        return;
    }

    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) {
        i++;
    }
    if (i > 0) {
        memmove(s, s + i, strlen(s + i) + 1);
    }

    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

void config_defaults(ServerConfig* cfg) {
    if (!cfg) {
        return;
    }
    snprintf(cfg->ip, sizeof(cfg->ip), "%s", "0.0.0.0");
    cfg->port = 7777;
    cfg->max_clients = 128;
    cfg->max_rooms = 32;
}

/**
 * @brief Applies a single key/value pair into the server configuration.
 *
 * @param cfg   Pointer to the configuration structure to update.
 * @param k     Key string
 * @param v     Value string
 */
static void set_kv(ServerConfig* cfg, const char* k, const char* v) {
    if (!cfg || !k || !v) {
        return;
    }

    if (strcmp(k, "ip") == 0) {
        snprintf(cfg->ip, sizeof(cfg->ip), "%s", v);
        return;
    }
    if (strcmp(k, "port") == 0) {
        cfg->port = atoi(v);
        return;
    }
    if (strcmp(k, "max_clients") == 0) {
        cfg->max_clients = atoi(v);
        return;
    }
    if (strcmp(k, "max_rooms") == 0) {
        cfg->max_rooms = atoi(v);
        return;
    }
}

int config_load_file(ServerConfig* cfg, const char* path) {
    if (!cfg || !path) {
        return 0;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }
        char* semi = strchr(line, ';');
        if (semi) {
            *semi = '\0';
        }

        trim(line);
        if (line[0] == '\0') {
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        char* k = line;
        char* v = eq + 1;
        trim(k);
        trim(v);

        if (k[0] == '\0' || v[0] == '\0') {
            continue;
        }

        set_kv(cfg, k, v);
    }

    fclose(f);
    return 1;
}

void config_print(const ServerConfig* cfg) {
    if (!cfg) {
        return;
    }
    printf("config: ip = %s, port = %d, max_clients = %d, max_rooms = %d\n", cfg->ip, cfg->port, cfg->max_clients, cfg->max_rooms);
}
