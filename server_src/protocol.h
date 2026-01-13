/**
 * @file protocol.h
 * @brief Text-based protocol parser
 * 
 * Seminar work of "Fundamentals of Computer Networks"
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#pragma once
#include <stddef.h>

#define MAX_KV 32
#define MAX_KEY 32
#define MAX_VAL 128
#define MAX_CMD 32

/**
 * @brief Protocol message type.
 */
typedef enum {
    PT_REQ  = 1,    // Client request
    PT_RESP = 2,    // Server response
    PT_EVT  = 3,    // Server event
    PT_ERR  = 4     // Error response
} ProtoType;

/**
 * @brief Protocol parse result.
 */
typedef enum {
    PROTO_OK  = 0,  // Parsing successful
    PROTO_BAD = 1   // Parsing failed
} ProtoResult;

/**
 * @brief Key-value pair in protocol message
 */
typedef struct {
    char key[MAX_KEY];
    char val[MAX_VAL];
} KV;

/**
 * @brief Parsed protocol message.
 */
typedef struct {
    ProtoType type;     // Message type
    char cmd[MAX_CMD];  // Command name
    KV kv[MAX_KV];      // Key-value pairs
    int kv_count;       // Number of key-value pairs
} ProtoMsg;

/**
 * @brief Parses a protocol line into a ProtoMsg.
 *
 * @param line  Input line
 * @param out   Output structure
 *
 * @return PROTO_OK on success, PROTO_BAD on failure
 */
ProtoResult proto_parse(const char* line, ProtoMsg* out);

/**
 * @brief Retrieves value for a key from a parsed message
 *
 * @param m     Parsed message
 * @param key   Key name
 *
 * @return Pointer to value string, or NULL if not found
 */
const char* proto_get(const ProtoMsg* m, const char* key);

#endif
