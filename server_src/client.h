/**
 * @file client.h
 * @brief Client (player) representation for the server
 *
 * Seminar work of "Fundamentals of Computer Networks"
 */

#ifndef CLIENT_H
#define CLIENT_H

#pragma once
#include <stddef.h>
#include <time.h>

#define BUF_SIZE 8192

/**
 * @brief Client slot state
 */
typedef enum {
    C_EMPTY = 0,    // Slot is unused
    C_CONNECTED,    // Slot contains a client (online or offline)
} ClientSlot;

/**
 * @brief Runtime representation of a client
 *
 * A client may be online or offline
 * Slot is freed only after offline timeout expires
 */
typedef struct {
    ClientSlot slot;        // Slot usage state
    int fd;                 // Socket file descriptor, -1 if offline

    char nick[32];          // Player nickname
    char session[64];       // Session token for RESUME

    int room_id;            // Current room ID, -1 if none
    int in_game;            // Non-zero if currently in a running game

    char rbuf[BUF_SIZE];    // Receive buffer
    size_t rlen;            // Number of bytes currently in rbuf

    int strikes;            // Protocol parse error counter
    time_t last_seen;       // Last activity timestamp (online/offline)

    int online;             // 1 if connected, 0 if offline
} Client;

#endif
