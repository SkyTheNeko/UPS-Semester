#include "lobby.h"
#include "client.h"
#include "game.h"
#include "protocol.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>

#define MAX_ROOMS 64
#define MAX_ROOM_PLAYERS 4
#define OFFLINE_TIMEOUT_SEC 120

/**
 * @brief Room lifecycle state
 */
typedef enum { 
    ROOM_EMPTY=0, 
    ROOM_LOBBY=1, 
    ROOM_GAME=2 
} RoomPhase;

typedef struct {
    int used;               // Whether this room slot is currently allocated and valid
    int id;                 // Unique room identifier visible to clients
    char name[32];          // Room name 
    int size;               // Target room capacity (2-4)

    RoomPhase phase;        // Current room phase
    int paused;             // Whether the running game is currently paused due to an offline player
    time_t pause_started;

    int players[MAX_ROOM_PLAYERS];  // Client indices of players in this room
    int pcount;             // Current number of players present in the room
    int host_idx;           // Client index of the host

    Game game;              // Game state for this room
} Room;

static SendLineFn g_send;   // Function used to send a raw protocol line to a client
static SendErrFn g_err;     // Function used to send an error response to a client
static Client* g_clients;   // Pointer to the global client array
static int g_max_clients;   // Maximum number of clients available

static int g_limit_rooms=MAX_ROOMS;   // Runtime limit for number of rooms that can be allocated

static Room g_rooms[MAX_ROOMS]; // Fixed-size room storage
static int g_next_room_id=1;    // Auto-increment room id

/**
 * @brief Sends a formatted protocol line to a single client
 *
 * Formats a message using printf-like formatting and sends it
 *
 * @param c     Client index
 * @param fmt   String for the outgoing line
 * @param ...   Format arguments
 */
static void sendf(int c, const char* fmt, ...) {
    char out[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out, sizeof(out), fmt, ap);
    va_end(ap);
    g_send(c, out);
}

/**
 * @brief Sends current room/game state to a client
 *
 * @param r     Pointer to the room
 * @param ci    Target client index
 */
static void room_send_state(Room* r, int ci) {
    const char* phase = (r->phase == ROOM_GAME) ? "GAME" : "LOBBY";

    char top[4] = "-";
    if (r->phase == ROOM_GAME) card_to_str(r->game.top_card, top);

    const char* turn_nick="-";
    if (r->phase == ROOM_GAME && r->pcount > 0) {
        int tci = r->players[r->game.turn_pos];
        if (tci >= 0) {
            turn_nick=g_clients[tci].nick;
        }
    }

    sendf(ci, "EVT STATE room=%d phase=%s paused=%d top=%s active_suit=%c penalty=%d turn=%s\n", 
        r->id, phase, r->paused ? 1 : 0, top, r->game.active_suit ? r->game.active_suit : '-', r->game.penalty, turn_nick
    );
}

/**
 * @brief Sends the full player roster and online state to a specific client
 *
 * @param r     Pointer to the room.
 * @param to_ci Target client index.
 */
static void room_send_roster(Room* r, int to_ci) {
    if (!r) {
        return;
    }
    if (to_ci < 0) {
        return;
    }
    if (g_clients[to_ci].slot == C_EMPTY) {
        return;
    }

    if (r->host_idx >= 0 && g_clients[r->host_idx].nick[0]) {
        sendf(to_ci, "EVT HOST nick=%s\n", g_clients[r->host_idx].nick);
    }

    for (int i = 0; i < r->pcount; i++) {
        int ci = r->players[i];
        if (ci < 0) {
            continue;
        }
        if (g_clients[ci].slot == C_EMPTY) {
            continue;
        }
        if (!g_clients[ci].nick[0]) {
            continue;
        }

        sendf(to_ci, "EVT PLAYER_JOIN nick=%s\n", g_clients[ci].nick);

        if (g_clients[ci].online && g_clients[ci].fd >= 0) {
            sendf(to_ci, "EVT PLAYER_ONLINE nick=%s\n", g_clients[ci].nick);
        } 
        else {
            sendf(to_ci, "EVT PLAYER_OFFLINE nick=%s\n", g_clients[ci].nick);
        }
    }
}

/**
 * @brief Broadcasts a protocol line to all online clients in a room.
 *
 * Sends the given line to every player currently present in the room who is considered online and has a valid socket descriptor
 *
 * @param r     Pointer to the room
 * @param line  Line to broadcast
 */
static void room_broadcast(Room* r, const char* line) {
    for (int i = 0; i < r->pcount; i++) {
        int ci = r->players[i];
        if (ci >= 0 && g_clients[ci].slot != C_EMPTY && g_clients[ci].online && g_clients[ci].fd >= 0) {
            g_send(ci, line);
        }
    }
}

/**
 * @brief Broadcasts a formatted protocol line to all online clients in a room
 *
 * Formats a formatted message sends it to all online players in the room
 *
 * @param r     Pointer to the room.
 * @param fmt   String for the outgoing line
 * @param ...   Format arguments
 */
static void room_broadcastf(Room* r, const char* fmt, ...) {
    char out[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out, sizeof(out), fmt, ap);
    va_end(ap);
    room_broadcast(r, out);
}

/**
 * @brief Broadcasts the current state to all online players in the room
 *
 * @param r     Pointer to the room
 */
static void room_broadcast_state(Room* r) {
    for (int i = 0; i < r->pcount; i++) {
        int ci = r->players[i];
        if (ci >= 0 && g_clients[ci].slot != C_EMPTY && g_clients[ci].online && g_clients[ci].fd >= 0) {
            room_send_state(r, ci);
        }
    }
}

/**
 * @brief Broadcasts a line to all online players except one client
 *
 * @param r         Pointer to the room
 * @param except_ci Client index to skip
 * @param line      Line to broadcast
 */
static void room_broadcast_except(Room* r, int except_ci, const char* line) {
    for (int i = 0; i < r->pcount; i++) {
        int ci = r->players[i];
        if (ci == except_ci) {
            continue;
        }
        if (ci >= 0 && g_clients[ci].slot != C_EMPTY && g_clients[ci].online && g_clients[ci].fd >= 0) {
            g_send(ci, line);
        }
    }
}

/**
 * @brief Checks whether a client is logged in
 *
 * A client is considered logged in if they have both a non-empty nickname and a non-empty session token
 *
 * @param c     Client index
 *
 * @return 1 if logged in, 0 otherwise
 */
static int is_logged(int c) {
    return g_clients[c].nick[0] != '\0' && g_clients[c].session[0] != '\0';
}

/**
 * @brief Finds an existing client slot by nickname
 *
 * Searches the global client array for a non-empty slot with a matching nick
 *
 * @param nick  Nickname to search for
 *
 * @return Client index if found, -1 otherwise
 */
static int find_client_by_nick(const char* nick) {
    for (int i = 0; i < g_max_clients; i++) {
        if (g_clients[i].slot != C_EMPTY && g_clients[i].nick[0] != '\0') {
            if (strcmp(g_clients[i].nick, nick) == 0) {
                return i;
            }
        }
    }
    return -1;
}

/**
 * @brief Locates a room by its room id
 *
 * Searches the room table up to the runtime room limit and returns the room pointer if the room exists and is marked as used
 *
 * @param id    Room id
 *
 * @return Pointer to the room if found, NULL otherwise
 */
static Room* room_by_id(int id) {
    for (int i = 0; i < g_limit_rooms; i++) {
        if (g_rooms[i].used && g_rooms[i].id == id) {
            return &g_rooms[i];
        }
    }
    return NULL;
}

/**
 * @brief Returns the player position of a client inside a room
 *
 * @param r             Pointer to the room
 * @param client_idx    Client index to locate
 *
 * @return Player position if found, -1 otherwise
 */
static int room_pos_of(Room* r, int client_idx) {
    for (int i = 0; i < r->pcount; i++) {
        if (r->players[i] == client_idx) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Generates a new session token string
 *
 * Creates a pseudo-random session token and writes it into a provided output buffer
 *
 * @param out Output buffer
 */
static void make_session(char out[64]) {
    unsigned int a = (unsigned int)rand();
    unsigned int b = (unsigned int)time(NULL);
    snprintf(out, 64, "%08x%08x%08x%08x", a, b, (unsigned int)rand(), (unsigned int)rand());
}

/**
 * @brief Checks whether a client is currently active
 *
 * @param ci    Client index
 *
 * @return 1 if active, 0 otherwise
 */
static int client_is_active(int ci) {
    if (ci < 0) {
        return 0;
    }
    if (g_clients[ci].slot == C_EMPTY) {
        return 0;
    }
    if (!g_clients[ci].online) {
        return 0;
    }
    if (g_clients[ci].fd < 0) {
        return 0;
    }

    return 1;
}

/**
 * @brief Tests whether any player in the room is currently offline
 *
 * @param r     Pointer to the room
 *
 * @return 1 if at least one player is offline, 0 otherwise
 */
static int room_any_offline(Room* r) {
    if (!r || !r->used) {
        return 0;
    }
    for (int i = 0; i < r->pcount; i++) {
        int ci = r->players[i];
        if (!client_is_active(ci)) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Returns the client index of the first offline player in a room
 *
 * @param r     Pointer to the room
 *
 * @return Client index of an offline player, or -1 if all are online
 */
static int room_first_offline(Room* r) {
    if (!r || !r->used) {
        return -1;
    }
    for (int i = 0; i < r->pcount; i++) {
        int ci = r->players[i];
        if (!client_is_active(ci)) {
            return ci;
        }
    }
    return -1;
}

/**
 * @brief Pauses an active game if not already paused
 *
 * @param r             Pointer to the room
 * @param reason_nick   Nickname that caused the pause
 */
static void room_pause(Room* r, const char* reason_nick) {
    if (!r || !r->used) {
        return;
    }
    if (r->phase != ROOM_GAME) {
        return;
    }
    if (r->paused) {
        return;
    }

    r->paused = 1;
    r->pause_started = time(NULL);

    if (reason_nick && reason_nick[0]) {
        room_broadcastf(r, "EVT GAME_PAUSED nick=%s timeout=%d\n", reason_nick, OFFLINE_TIMEOUT_SEC);
    } 
    else {
        room_broadcastf(r, "EVT GAME_PAUSED timeout=%d\n", OFFLINE_TIMEOUT_SEC);
    }
}

/**
 * @brief Resumes a paused game if all players are online again
 *
 * @param r     Pointer to the room
 */
static void room_resume(Room* r) {
    if (!r || !r->used) {
        return;
    }
    if (r->phase != ROOM_GAME) {
        return;
    }
    if (!r->paused) {
        return;
    }

    if (!room_any_offline(r)) {
        r->paused = 0;
        r->pause_started = 0;
        room_broadcast(r, "EVT GAME_RESUMED\n");
    }
}

/**
 * @brief Aborts an active game and returns the room into lobby state.
 *
 * @param r         Pointer to the room
 * @param reason    Abort reason string
 */
static void room_abort_game(Room* r, const char* reason) {
    if (!r || !r->used) {
        return;
    }
    if (r->phase != ROOM_GAME) {
        return;
    }

    r->phase = ROOM_LOBBY;
    r->paused = 0;
    r->pause_started = 0;

    for (int i = 0; i < r->pcount; i++) {
        int ci = r->players[i];
        if (ci >= 0 && g_clients[ci].slot != C_EMPTY) {
            g_clients[ci].in_game = 0;
        }
    }

    memset(&r->game, 0, sizeof(r->game));

    if (!reason) {
        reason = "offline_timeout";
    }
    room_broadcastf(r, "EVT GAME_ABORT reason=%s\n", reason);

    room_broadcast_state(r);
}


/**
 * @brief Sends the current hand of one player to that player
 *
 * @param r     Pointer to the room
 * @param ppos  Player position
 */
static void room_send_hand(Room* r, int ppos) {
    int ci = r->players[ppos];
    if (ci < 0) {
        return;
    }

    char cards[512];
    cards[0] = '\0';

    for (int i = 0; i < r->game.hand_count[ppos]; i++) {
        char cs[4];
        card_to_str(r->game.hands[ppos][i], cs);
        strcat(cards, cs);
        if (i + 1 < r->game.hand_count[ppos]) {
            strcat(cards, ",");
        }
    }

    sendf(ci, "EVT HAND cards=%s\n", cards);
}

/**
 * @brief Removes a client from the room player list
 *
 * Updates the players array, decreases player count, reassigns host if the host left, and deletes the room if it becomes empty
 *
 * @param r             Pointer to the room
 * @param client_idx    Client index to remove
 */
static void room_remove_player(Room* r, int client_idx) {
    int pos = room_pos_of(r, client_idx);
    if (pos < 0) {
        return;
    }

    for (int i = pos; i < r->pcount - 1; i++) {
        r->players[i] = r->players[i + 1];
    }
    r->players[r->pcount - 1] = -1;
    r->pcount--;

    if (r->host_idx == client_idx && r->pcount > 0) {
        r->host_idx = r->players[0];
        room_broadcastf(r, "EVT HOST nick=%s\n", g_clients[r->host_idx].nick);
    }

    if (r->pcount == 0) {
        memset(r, 0, sizeof(*r));
    }
}

/**
 * @brief Removes a player during an active game (compacts both room and game state).
 *
 * Removes the player at the given player position, shifts room players and also shifts game hands/hand counts. Adjusts turn position so the game can continue with remaining players
 *
 * @param r             Pointer to the room
 * @param removed_ppos  Player position to remove
 */
static void room_remove_player_in_game(Room* r, int removed_ppos) {
    if (!r) {
        return;
    }
    if (removed_ppos < 0 || removed_ppos >= r->pcount) {
        return;
    }

    int old_pcount = r->pcount;

    if (r->game.turn_pos > removed_ppos) {
        r->game.turn_pos--;
    }

    for (int i = removed_ppos; i < old_pcount - 1; i++) {
        r->players[i] = r->players[i + 1];
    }
    r->players[old_pcount - 1] = -1;
    r->pcount--;

    for (int i = removed_ppos; i < old_pcount - 1; i++) {
        r->game.hand_count[i] = r->game.hand_count[i + 1];
        for (int k = 0; k < MAX_HAND; k++) {
            r->game.hands[i][k] = r->game.hands[i + 1][k];
        }
    }
    r->game.hand_count[old_pcount - 1] = 0;
    for (int k = 0; k < MAX_HAND; k++) {
        r->game.hands[old_pcount - 1][k] = 0;
    }

    if (r->pcount > 0) {
        if (r->game.turn_pos >= r->pcount) {
            r->game.turn_pos = 0;
        }
        if (r->game.turn_pos < 0) {
            r->game.turn_pos = 0;
        }
    } 
    else {
        r->game.turn_pos = 0;
    }

    if (r->host_idx >= 0) {
        int host_still_inside = 0;
        for (int i = 0; i < r->pcount; i++) {
            if (r->players[i] == r->host_idx) {
                host_still_inside = 1;
                break;
            }
        }
        if (!host_still_inside && r->pcount > 0) {
            r->host_idx = r->players[0];
            room_broadcastf(r, "EVT HOST nick=%s\n", g_clients[r->host_idx].nick);
        }
    }

    if (r->pcount == 0) {
        memset(r, 0, sizeof(*r));
    }
}

/**
 * @brief Validates that a client is allowed to perform an in-game action
 *
 * @param client_idx    Client index performing the action
 * @param out_r         Output pointer to the room on success
 * @param out_ppos      Output player position on success
 *
 * @return 1 if valid, 0 otherwise
 */
static int ensure_in_game(int client_idx, Room** out_r, int* out_ppos) {
    int rid=g_clients[client_idx].room_id;
    if (rid < 0) { 
        return 0;
    }
    Room* r = room_by_id(rid);
    if (!r) {
        return 0;
    }
    if (r->phase != ROOM_GAME) {
        return 0;
    }
    if (r->paused) {
        return 0;
    }
    int ppos = room_pos_of(r, client_idx);
    if (ppos < 0) {
        return 0;
    }
    *out_r = r;
    *out_ppos = ppos;

    return 1;
}

void lobby_init(SendLineFn s, SendErrFn e, void* clients_array, int max_clients, int max_rooms) {
    g_send = s;
    g_err = e;
    g_clients = (Client*)clients_array;
    g_max_clients = max_clients;

    g_limit_rooms=max_rooms;
    if (g_limit_rooms < 1) {
        g_limit_rooms=1;
    }
    if (g_limit_rooms > MAX_ROOMS) {
        g_limit_rooms=MAX_ROOMS;
    }

    memset(g_rooms, 0, sizeof(g_rooms));
    g_next_room_id=1;

    srand((unsigned int)time(NULL));
}

void lobby_tick(void) {
    time_t now = time(NULL);

    for (int ri = 0; ri < g_limit_rooms; ri++) {
        if (!g_rooms[ri].used) {
            continue;
        }
        Room* r = &g_rooms[ri];

        if (r->phase == ROOM_GAME) {
            if (room_any_offline(r)) {
                int off = room_first_offline(r);
                const char* who = (off >= 0 ? g_clients[off].nick : "");
                room_pause(r, who);

                if (r->paused && r->pause_started > 0) {
                    if ((int)(now - r->pause_started) > OFFLINE_TIMEOUT_SEC) {
                        room_abort_game(r, "reconnect_timeout");
                        room_broadcast_state(r);
                    }
                }
            }
            else {
                if (r->paused) {
                    room_resume(r);
                    room_broadcast_state(r);
                }
            }
        }
    }

    for (int i = 0; i < g_max_clients; i++) {
        if (g_clients[i].slot == C_EMPTY) {
            continue;
        }
        if (g_clients[i].online) {
            continue;
        }

        if ((int)(now - g_clients[i].last_seen) > OFFLINE_TIMEOUT_SEC) {
            int rid=g_clients[i].room_id;
            if (rid >= 0) {
                Room* r = room_by_id(rid);
                if (r) {
                    room_broadcastf(r, "EVT PLAYER_LEAVE nick=%s\n", g_clients[i].nick);

                    if (r->phase == ROOM_GAME) {
                        room_abort_game(r, "player_removed");
                    }

                    room_remove_player(r, i);

                    if (r->used && r->pcount > 0) {
                        room_broadcast_state(r);
                    }
                }
            }

            g_clients[i].nick[0] = '\0';
            g_clients[i].session[0] = '\0';
            g_clients[i].room_id=-1;
            g_clients[i].in_game = 0;
            g_clients[i].fd = -1;
            g_clients[i].slot = C_EMPTY;
        }
    }
}

void lobby_on_disconnect(int client_idx) {
    if (client_idx < 0 || client_idx >= g_max_clients) {
        return;
    }

    Client* c = &g_clients[client_idx];
    c->online = 0;
    c->last_seen = time(NULL);

    if (c->room_id < 0) {
        return;
    }
    Room* r = room_by_id(c->room_id);
    if (!r) {
        c->room_id=-1;
        return;
    }

    room_broadcastf(r, "EVT PLAYER_OFFLINE nick=%s\n", c->nick);

    if (r->phase == ROOM_GAME) {
        room_pause(r, c->nick);
        room_broadcast_state(r);
    }
}

void lobby_handle_login(int client_idx, const char* nick) {
    Client* c = &g_clients[client_idx];
    c->online = 1;

    if (!nick || !nick[0]) {
        g_err(client_idx, "LOGIN", "BAD_FORMAT", "missing_nick");
        return;
    }
    if (strlen(nick) >= sizeof(c->nick)) {
        g_err(client_idx, "LOGIN", "INVALID_VALUE", "nick_too_long");
        return;
    }

    int existing = find_client_by_nick(nick);
    if (existing >= 0 && existing != client_idx) {
        if (!g_clients[existing].online) {
            g_err(client_idx, "LOGIN", "NICK_TAKEN", "use_resume_offline");
        }
        else {
            g_err(client_idx, "LOGIN", "NICK_TAKEN", "already_online");
        }
        return;
    }

    snprintf(c->nick, sizeof(c->nick), "%s", nick);
    make_session(c->session);

    c->room_id=-1;
    c->in_game = 0;

    sendf(client_idx, "RESP LOGIN ok=1 session=%s\n", c->session);
}

void lobby_handle_logout(int client_idx) {
    if (client_idx < 0 || client_idx >= g_max_clients) return;

    Client* c = &g_clients[client_idx];

    int rid=c->room_id;

    if (rid >= 0) {
        Room* r = room_by_id(rid);
        if (r) {
            room_broadcastf(r, "EVT PLAYER_LEAVE nick=%s\n", c->nick);

            if (r->phase == ROOM_GAME) {
                room_abort_game(r, "logout");
            }

            room_remove_player(r, client_idx);

            if (r->used && r->pcount > 0) {
                room_broadcast_state(r);
            }
        }
    }

    if (c->fd >= 0) {
        sendf(client_idx, "RESP LOGOUT ok=1\n");
        close(c->fd);
    }

    c->fd = -1;
    c->online = 0;
    c->last_seen = time(NULL);

    c->nick[0] = '\0';
    c->session[0] = '\0';
    c->room_id=-1;
    c->in_game = 0;

    memset(c->rbuf, 0, sizeof(c->rbuf));
    c->rlen = 0;
    c->strikes = 0;

    c->slot = C_EMPTY;
}

void lobby_handle_resume(int client_idx, const char* nick, const char* session) {
    Client* c = &g_clients[client_idx];
    c->online = 1;
    c->last_seen = time(NULL);

    int existing = find_client_by_nick(nick);
    if (existing < 0) {
        g_err(client_idx, "RESUME", "BAD_SESSION", "no_such_nick");
        return;
    }

    if (strcmp(g_clients[existing].session, session) != 0) {
        g_err(client_idx, "RESUME", "BAD_SESSION", "token");
        return;
    }

    if (existing != client_idx && g_clients[existing].online) {
        g_err(client_idx, "RESUME", "ALREADY_ONLINE", "use_login");
        return;
    }

    if (existing != client_idx) {
        Client* old = &g_clients[existing];

        char tmp_nick[32];
        char tmp_ses[64];
        snprintf(tmp_nick, sizeof(tmp_nick), "%s", old->nick);
        snprintf(tmp_ses,  sizeof(tmp_ses),  "%s", old->session);

        snprintf(c->nick,    sizeof(c->nick),    "%s", tmp_nick);
        snprintf(c->session, sizeof(c->session), "%s", tmp_ses);

        c->room_id=old->room_id;
        c->in_game = old->in_game;

        if (c->room_id >= 0) {
            Room* r = room_by_id(c->room_id);
            if (r) {
                for (int i = 0; i < r->pcount; i++) {
                    if (r->players[i] == existing) r->players[i] = client_idx;
                }
                if (r->host_idx == existing) r->host_idx = client_idx;
            }
        }

        memset(old, 0, sizeof(*old));
        old->slot = C_EMPTY;
    }

    sendf(client_idx, "RESP RESUME ok=1\n");

    if (c->room_id >= 0) {
        Room* r = room_by_id(c->room_id);
        if (r) {
            char msg[128];
            snprintf(msg, sizeof(msg), "EVT PLAYER_ONLINE nick=%s\n", c->nick);
            room_broadcast_except(r, client_idx, msg);

            room_send_roster(r, client_idx);
            room_send_state(r, client_idx);

            if (r->phase == ROOM_GAME) {
                int ppos = room_pos_of(r, client_idx);
                if (ppos >= 0) {
                    room_send_hand(r, ppos);
                }

                char top[4];
                card_to_str(r->game.top_card, top);
                sendf(client_idx, "EVT TOP card=%s active_suit=%c penalty=%d\n",
                    top, r->game.active_suit ? r->game.active_suit : '-',
                    r->game.penalty);

                int tci = r->players[r->game.turn_pos];
                const char* tn = (tci >= 0) ? g_clients[tci].nick : "-";
                sendf(client_idx, "EVT TURN nick=%s\n", tn);

                if (r->paused) {
                    room_resume(r);
                    room_broadcast_state(r);
                }
            }
        }
    }
}

void lobby_handle_list_rooms(int client_idx) {
    if (!is_logged(client_idx)) {
        g_err(client_idx, "LIST_ROOMS", "NOT_LOGGED", "login_first");
        return;
    }

    int count = 0;
    for (int i = 0; i < g_limit_rooms; i++) {
        if (g_rooms[i].used) {
            count++;
        }
    }

    sendf(client_idx, "RESP LIST_ROOMS ok=1 rooms=%d\n", count);

    for (int i = 0; i < g_limit_rooms; i++) {
        if (!g_rooms[i].used) {
            continue;
        }
        Room* r = &g_rooms[i];
        const char* st = (r->phase == ROOM_GAME) ? "GAME" : "LOBBY";
        sendf(client_idx, "EVT ROOM id=%d name=%s players=%d/%d state=%s\n", r->id, r->name, r->pcount, r->size, st);
    }
}

void lobby_handle_create_room(int client_idx, const char* name, int size) {
    if (!is_logged(client_idx)) {
        g_err(client_idx, "CREATE_ROOM", "NOT_LOGGED", "login_first");
        return;
    }
    if (g_clients[client_idx].room_id >= 0) {
        g_err(client_idx, "CREATE_ROOM", "BAD_STATE", "already_in_room");
        return;
    }

    if (!name || !name[0]) {
        g_err(client_idx, "CREATE_ROOM", "BAD_FORMAT", "missing_name");
        return;
    }
    if (size < 2 || size > 4) {
        g_err(client_idx, "CREATE_ROOM", "INVALID_VALUE", "size_2_4");
        return;
    }

    int slot = -1;
    for (int i = 0; i < g_limit_rooms; i++) {
        if (!g_rooms[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        g_err(client_idx, "CREATE_ROOM", "LIMIT_REACHED", "max_rooms");
        return;
    }

    Room* r = &g_rooms[slot];
    memset(r, 0, sizeof(*r));
    r->used = 1;
    r->id=g_next_room_id++;
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->size = size;
    r->phase = ROOM_LOBBY;
    r->paused = 0;
    r->pause_started = 0;
    for (int i = 0; i < MAX_ROOM_PLAYERS; i++) {
        r->players[i] = -1;
    }

    r->players[0] = client_idx;
    r->pcount = 1;
    r->host_idx = client_idx;

    g_clients[client_idx].room_id=r->id;
    g_clients[client_idx].in_game = 0;

    sendf(client_idx, "RESP CREATE_ROOM ok=1 room=%d\n", r->id);
    room_broadcastf(r, "EVT PLAYER_JOIN nick=%s\n", g_clients[client_idx].nick);
    room_broadcastf(r, "EVT HOST nick=%s\n", g_clients[r->host_idx].nick);
    room_broadcast_state(r);
}

void lobby_handle_join_room(int client_idx, int room_id) {
    if (!is_logged(client_idx)) {
        g_err(client_idx, "JOIN_ROOM", "NOT_LOGGED", "login_first");
        return;
    }
    if (g_clients[client_idx].room_id >= 0) {
        g_err(client_idx, "JOIN_ROOM", "BAD_STATE", "already_in_room");
        return;
    }

    Room* r = room_by_id(room_id);
    if (!r) {
        g_err(client_idx, "JOIN_ROOM", "NO_SUCH_ROOM", "id");
        return;
    }
    if (r->phase != ROOM_LOBBY) {
        g_err(client_idx, "JOIN_ROOM", "BAD_STATE", "game_running");
        return;
    }
    if (r->pcount >= r->size) {
        g_err(client_idx, "JOIN_ROOM", "ROOM_FULL", "full");
        return;
    }

    r->players[r->pcount++] = client_idx;
    g_clients[client_idx].room_id=r->id;
    g_clients[client_idx].in_game = 0;

    sendf(client_idx, "RESP JOIN_ROOM ok=1 room=%d\n", r->id);
    room_send_roster(r, client_idx);
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "EVT PLAYER_JOIN nick=%s\n", g_clients[client_idx].nick);
        room_broadcast_except(r, client_idx, msg);
    }
    room_send_state(r, client_idx);
    room_broadcast_state(r);
}

void lobby_handle_leave_room(int client_idx) {
    if (!is_logged(client_idx)) {
        g_err(client_idx, "LEAVE_ROOM", "NOT_LOGGED", "login_first");
        return;
    }

    int rid=g_clients[client_idx].room_id;
    if (rid < 0) {
        g_err(client_idx, "LEAVE_ROOM", "BAD_STATE", "not_in_room");
        return;
    }

    Room* r = room_by_id(rid);
    if (!r) {
        g_clients[client_idx].room_id=-1;
        g_clients[client_idx].in_game = 0;
        sendf(client_idx, "RESP LEAVE_ROO ok=1\n");
        return;
    }

    room_broadcastf(r, "EVT PLAYER_LEAVE nick=%s\n", g_clients[client_idx].nick);

    if (r->phase == ROOM_GAME) {
        int removed_ppos = room_pos_of(r, client_idx);
        if (removed_ppos >= 0) {
            room_remove_player_in_game(r, removed_ppos);
        } 
        else {
            room_remove_player(r, client_idx);
        }
    } 
    else {
        room_remove_player(r, client_idx);
    }

    g_clients[client_idx].room_id=-1;
    g_clients[client_idx].in_game = 0;

    sendf(client_idx, "RESP LEAVE_ROOM ok=1\n");

    if (!r->used) return;

    if (r->phase == ROOM_GAME) {
        if (r->pcount < 2) {
            if (r->pcount == 1) {
                int wci = r->players[0];
                if (wci >= 0 && g_clients[wci].nick[0]) {
                    room_broadcastf(r, "EVT GAME_END winner=%s\n", g_clients[wci].nick);
                }
            } 
            else {
                room_broadcast(r, "EVT GAME_ABORT reason=not_enough_players\n");
            }

            r->phase = ROOM_LOBBY;
            r->game.running = 0;
            for (int i = 0; i < r->pcount; i++) {
                g_clients[r->players[i]].in_game = 0;
            }

            room_broadcast_state(r);
            return;
        }

        for (int ppos = 0; ppos < r->pcount; ppos++) {
            room_send_hand(r, ppos);
        }
        {
            int tci = r->players[r->game.turn_pos];
            if (tci >= 0 && g_clients[tci].nick[0]) {
                room_broadcastf(r, "EVT TURN nick=%s\n", g_clients[tci].nick);
            }
        }

        room_broadcast_state(r);
        return;
    }

    if (r->used && r->pcount > 0) {
        room_broadcast_state(r);
    }
}



void lobby_handle_start_game(int client_idx) {
    if (!is_logged(client_idx)) {
        g_err(client_idx, "START_GAME", "NOT_LOGGED", "login_first");
        return;
    }

    int rid=g_clients[client_idx].room_id;
    if (rid < 0) {
        g_err(client_idx, "START_GAME", "BAD_STATE", "not_in_room"); 
        return;
    }

    Room* r = room_by_id(rid);
    if (!r) {
        g_err(client_idx, "START_GAME", "BAD_STATE", "no_room");
        return;
    }
    if (r->phase != ROOM_LOBBY) {
        g_err(client_idx, "START_GAME", "BAD_STATE", "already_running");
        return;
    }
    if (r->host_idx != client_idx) {
        g_err(client_idx, "START_GAME", "NOT_HOST", "host_only");
        return;
    }
    if (r->pcount < 2) {
        g_err(client_idx, "START_GAME", "NOT_ENOUGH_PLAYERS", "need_at_least_two");
        return;
    }

    init(&r->game, r->pcount, (unsigned int)time(NULL) ^ (unsigned int)r->id);
    deal(&r->game, r->pcount, 4);
    pick_start_top(&r->game);

    r->phase = ROOM_GAME;
    r->paused = 0;
    r->pause_started = 0;

    for (int i = 0; i < r->pcount; i++) {
        g_clients[r->players[i]].in_game = 1;
    }

    sendf(client_idx, "RESP START_GAME ok=1\n");

    room_broadcastf(r, "EVT GAME_START players=%d\n", r->pcount);

    for (int p = 0; p < r->pcount; p++) {
        room_send_hand(r, p);
    }

    char top[4];
    card_to_str(r->game.top_card, top);
    room_broadcastf(r, "EVT TOP card=%s active_suit=%c penalty=%d\n", top, r->game.active_suit, r->game.penalty);

    int tci = r->players[r->game.turn_pos];
    room_broadcastf(r, "EVT TURN nick=%s\n", g_clients[tci].nick);

    room_broadcast_state(r);
}

void lobby_handle_play(int client_idx, const ProtoMsg* m) {
    int rid=g_clients[client_idx].room_id;
    if (rid >= 0) {
        Room* rr = room_by_id(rid);
        if (rr && rr->phase == ROOM_GAME && rr->paused) {
            g_err(client_idx, "PLAY", "PAUSED", "wait_for_reconnect");
            return;
        }
    }

    Room* r;
    int ppos;
    if (!ensure_in_game(client_idx, &r, &ppos)) {
        g_err(client_idx, "PLAY", "BAD_STATE", "no_game");
        return;
    }

    const char* scard = proto_get(m, "card");
    const char* wish  = proto_get(m, "wish");

    if (!scard) { 
        g_err(client_idx, "PLAY", "BAD_FORMAT", "missing_card");
        return; 
    }

    unsigned char card;
    if (!str_to_card(scard, &card)) {
        g_err(client_idx, "PLAY", "BAD_FORMAT", "bad_card");
        return;
    }

    Outcome o;
    char errc[32] = {0};
    if (!play(&r->game, r->pcount, ppos, card, wish, &o, errc)) {
        g_err(client_idx, "PLAY", errc[0] ? errc : "ILLEGAL", "rejected");
        return;
    }

    sendf(client_idx, "RESP PLAY ok=1\n");

    if (wish && wish[0] && scard[1] == 'Q') {
        room_broadcastf(r, "EVT PLAYED nick=%s card=%s wish=%c\n", g_clients[client_idx].nick, scard, wish[0]);
    } 
    else {
        room_broadcastf(r, "EVT PLAYED nick=%s card=%s\n", g_clients[client_idx].nick, scard);
    }

    char top[4];
    card_to_str(r->game.top_card, top);
    room_broadcastf(r, "EVT TOP card=%s active_suit=%c penalty=%d\n", top, r->game.active_suit, r->game.penalty);

    room_send_hand(r, ppos);

    if (r->game.ended && o.winner_pos >= 0) {
        int wci = r->players[o.winner_pos];
        room_broadcastf(r, "EVT GAME_END winner=%s\n", g_clients[wci].nick);

        r->phase = ROOM_LOBBY;
        r->paused = 0;
        r->pause_started = 0;
        for (int i = 0; i < r->pcount; i++) {
            g_clients[r->players[i]].in_game = 0;
        }

        room_broadcast_state(r);
        return;
    }

    int tci = r->players[r->game.turn_pos];
    room_broadcastf(r, "EVT TURN nick=%s\n", g_clients[tci].nick);

    room_broadcast_state(r);
}

void lobby_handle_draw(int client_idx) {
    int rid=g_clients[client_idx].room_id;
    if (rid >= 0) {
        Room* rr = room_by_id(rid);
        if (rr && rr->phase == ROOM_GAME && rr->paused) {
            g_err(client_idx, "DRAW", "PAUSED", "wait_for_reconnect");
            return;
        }
    }

    Room* r;
    int ppos;
    if (!ensure_in_game(client_idx, &r, &ppos)) {
        g_err(client_idx, "DRAW", "BAD_STATE", "no_game");
        return;
    }

    unsigned char drawn[MAX_HAND];
    int drawn_count = 0;
    char errc[32] = {0};

    if (!draw(&r->game, r->pcount, ppos, drawn, &drawn_count, errc)) {
        g_err(client_idx, "DRAW", errc[0] ? errc : "REJECTED", "rejected");
        return;
    }

    sendf(client_idx, "RESP DRAW ok=1 count=%d\n", drawn_count);

    room_send_hand(r, ppos);

    int tci = r->players[r->game.turn_pos];
    room_broadcastf(r, "EVT TURN nick=%s\n", g_clients[tci].nick);

    room_broadcast_state(r);
}
