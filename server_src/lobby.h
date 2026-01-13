/**
 * @file lobby.h
 * @brief Lobby and room management layer
 *
 * Handles login, resume, rooms, player membership, and high-level game control
 * 
 * Seminar work of "Fundamentals of Computer Networks"
 */

#ifndef LOBBY_H
#define LOBBY_H

#pragma once
#include "protocol.h"

/**
 * @brief Callback for sending protocol lines to clients
 */
typedef void (*SendLineFn)(int client_idx, const char* line);

/**
 * @brief Callback for sending protocol error responses
 */
typedef void (*SendErrFn)(int client_idx, const char* cmd, const char* code, const char* msg);

/**
 * @brief Initializes the lobby subsystem
 *
 * @param s             Callback for sending lines
 * @param e             Callback for sending errors
 * @param clients_array Pointer to Client array
 * @param max_clients   Maximum number of clients
 * @param max_rooms     Maximum number of rooms
 */
void lobby_init(SendLineFn s, SendErrFn e, void* clients_array, int max_clients, int max_rooms);

/**
 * @brief Periodic lobby maintenance
 *
 * Handles offline timeouts, room cleanup, and paused games
 */
void lobby_tick(void);

/**
 * @brief Notifies lobby about client disconnection
 *
 * @param client_idx    Client index
 */
void lobby_on_disconnect(int client_idx);

/**
 * @brief Handles a client login request
 *
 * Assigns a nickname to the client, generates a new session token, and marks the client as logged in
 *
 * @param client_idx    Index of the client in the client array
 * @param nick          Nickname requested by the client
 */
void lobby_handle_login(int client_idx, const char* nick);

/**
 * @brief Logs out a client from the server
 *
 * Removes the client from any room, notifies other players, closes the network connection, and frees the client slot
 *
 * @param client_idx    Index of the client in the client array
 */
void lobby_handle_logout(int client_idx);

/**
 * @brief Resumes a previously disconnected client session
 *
 * Reattaches the client to an existing offline session using a nickname and session token. If the client was in a room or an active game, the state is restored
 *
 * @param client_idx    Index of the newly connected client slot
 * @param nick          Nickname of the session to resume
 * @param session       Session token associated with the nickname
 */
void lobby_handle_resume(int client_idx, const char* nick, const char* session);

/**
 * @brief Sends a list of available rooms to the client
 *
 * Each room includes its ID, name, player count, and current state
 *
 * @param client_idx    Index of the requesting client
 */
void lobby_handle_list_rooms(int client_idx);

/**
 * @brief Creates a new room and assigns the client as its host
 *
 * The room is created in lobby state and the client automatically joins it as the first player
 *
 * @param client_idx    Index of the client creating the room.
 * @param name          Human-readable name of the room.
 * @param size          Maximum number of players (2–4).
 */
void lobby_handle_create_room(int client_idx, const char* name, int size);

/**
 * @brief Adds the client to an existing room
 *
 * The client joins the room if it exists, is not full, and is currently in lobby state
 *
 * @param client_idx    Index of the joining client
 * @param room_id       Identifier of the room to join
 */
void lobby_handle_join_room(int client_idx, int room_id);

/**
 * @brief Removes the client from their current room
 *
 * If the client is the host, host privileges are transferred
 * If the room becomes empty, it is destroyed
 *
 * @param client_idx    Index of the leaving client
 */
void lobby_handle_leave_room(int client_idx);

/**
 * @brief Starts the game in the client’s current room
 *
 * Only the room host may start the game. The game can start once at least two players are present in the room
 *
 * @param client_idx    Index of the client requesting game start
 */
void lobby_handle_start_game(int client_idx);

/**
 * @brief Handles a card play request from a client
 *
 * Validates the move according to game rules and updates the game state. Broadcasts the result to all players
 *
 * @param client_idx    Index of the playing client
 * @param m             Parsed protocol message containing play data
 */
void lobby_handle_play(int client_idx, const ProtoMsg* m);

/**
 * @brief Handles a draw-card request from a client
 *
 * The client draws one or more cards depending on the current penalty state and game rules
 *
 * @param client_idx    Index of the client drawing cards
 */
void lobby_handle_draw(int client_idx);


#endif
