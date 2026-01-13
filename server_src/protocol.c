#include "protocol.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/**
 * @brief Extracts the next whitespace-delimited token from a string
 *
 * @param s         Pointer to a string pointer
 * @param out       Output buffer
 * @param out_sz    Size of the output buffer in bytes
 *
 * @return 1 if a token was extracted, 0 if the end of the string was reached
 */
static int split_token(const char** s, char* out, size_t out_sz) {
    while (**s && isspace((unsigned char)**s)) {
        (*s)++;
    }
    if (!**s) {
        return 0;
    }
    size_t n = 0;
    while (**s && !isspace((unsigned char)**s)) {
        if (n + 1 < out_sz) {
            out[n++] = **s;
        }
        (*s)++;
    }
    out[n] = '\0';

    return 1;
}

/**
 * @brief Parses a token and appends it to a ProtoMsg
 *
 * @param m     Pointer to the message being populated
 * @param tok   Token
 */
static void parse_kv(ProtoMsg* m, const char* tok) {
    if (m->kv_count >= MAX_KV) {
        return;
    }
    const char* eq = strchr(tok, '=');
    if (!eq) {
        return;
    }
    size_t klen = (size_t)(eq - tok);
    size_t vlen = strlen(eq + 1);

    if (klen == 0 || klen >= MAX_KEY) {
        return;
    }
    if (vlen >= MAX_VAL) {
        vlen = MAX_VAL - 1;
    }

    memcpy(m->kv[m->kv_count].key, tok, klen);
    m->kv[m->kv_count].key[klen] = '\0';
    memcpy(m->kv[m->kv_count].val, eq + 1, vlen);
    m->kv[m->kv_count].val[vlen] = '\0';
    m->kv_count++;
}

ProtoResult proto_parse(const char* line, ProtoMsg* out) {
    memset(out, 0, sizeof(*out));
    const char* s = line;
    char t1[16], t2[MAX_CMD], tok[256];

    if (!split_token(&s, t1, sizeof(t1))) {
        return PROTO_BAD;
    }
    if (!split_token(&s, t2, sizeof(t2))) {
        return PROTO_BAD;
    }

    if (strcmp(t1, "REQ") == 0) {
        out->type = PT_REQ;
    }
    else if (strcmp(t1, "RESP") == 0) {
        out->type = PT_RESP;
    }
    else if (strcmp(t1, "EVT") == 0) {
        out->type = PT_EVT;
    }
    else if (strcmp(t1, "ERR") == 0) {
        out->type = PT_ERR;
    }
    else {
        return PROTO_BAD;
    }

    snprintf(out->cmd, sizeof(out->cmd), "%s", t2);

    while (split_token(&s, tok, sizeof(tok))) {
        parse_kv(out, tok);
    }
    return PROTO_OK;
}

const char* proto_get(const ProtoMsg* m, const char* key) {
    for (int i = 0; i < m->kv_count; i++) {
        if (strcmp(m->kv[i].key, key) == 0) {
            return m->kv[i].val;
        }
    }
    return NULL;
}
