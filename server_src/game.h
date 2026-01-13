/**
 * @file game.h
 * @brief Core game logic.
 *
 * Provides game state structures and functions for deck handling, card validation, turn management, and win conditions
 * 
 * Seminar work of "Fundamentals of Computer Networks"
 */

#ifndef GAME_H
#define GAME_H

#pragma once
#include "protocol.h"

#define MAX_PLAYERS 4
#define MAX_HAND 32

/**
 * @brief Runtime state of a game
 */
typedef struct {
    int running;                // Non-zero if game is running
    int ended;                  // Non-zero if game has ended

    unsigned char deck[32];     // Draw deck
    int deck_top;               // Index of next card to draw

    unsigned char discard[32];  // Discard pile
    int discard_top;            // Index of top discard card 

    unsigned char hands[MAX_PLAYERS][MAX_HAND]; // Player hands
    int hand_count[MAX_PLAYERS];                // Card counts per player

    unsigned char top_card;     // Top card on discard pile
    char active_suit;           // Active suit
    int penalty;                // Accumulated 7-penalty

    int turn_pos;               // Index of player whose turn it is
} Game;

/**
 * @brief Result of a PLAY action
 */
typedef struct {
    int skip_next;      // Non-zero if next player is skipped
    int added_penalty;  // Penalty added by the card
    int winner_pos;     // Winner position, -1 if none
} Outcome;

/**
 * @brief Initializes a new game
 *
 * @param g             Game structure to initialize
 * @param player_count  Number of players
 * @param seed          RNG seed
 */
void init(Game* g, int player_count, unsigned int seed);

/**
 * @brief Deals cards to players
 *
 * @param g             Game state
 * @param player_count  Number of players
 * @param cards_each    Cards per player
 */
void deal(Game* g, int player_count, int cards_each);

/**
 * @brief Picks the initial top card for the game
 *
 * @param g Game state
 */
void pick_start_top(Game* g);

/**
 * @brief Converts internal card representation to string
 *
 * @param c     Internal card value
 * @param out   Output buffer (at least 4 bytes)
 */
void card_to_str(unsigned char c, char out[4]);

/**
 * @brief Parses card string into internal representation
 *
 * @param s         Input string
 * @param out_card  Output card value
 *
 * @return 1 on success, 0 on failure
 */
int str_to_card(const char* s, unsigned char* out_card);

/**
 * @brief Checks whether a player has a specific card
 *
 * @param g     Game state
 * @param ppos  Player position
 * @param card  Card to check
 *
 * @return 1 if present, 0 otherwise
 */
int hand_has(const Game* g, int ppos, unsigned char card);

/**
 * @brief Advances the turn to the next player
 *
 * @param g             Game state
 * @param player_count  Number of players
 * @param skip_next     Whether to skip one player
 */
void advance_turn(Game* g, int player_count, int skip_next);

/**
 * @brief Attempts to play a card.
 *
 * @param g             Game state
 * @param player_count  Number of players
 * @param ppos          Player position
 * @param card          Card to play
 * @param wish          Optional suit wish (Queen)
 * @param out           Outcome structure to fill
 * @param err_code      Error code buffer
 *
 * @return 1 if move is valid and applied, 0 otherwise.
 */
int play(
    Game* g,
    int player_count,
    int ppos,
    unsigned char card,
    const char* wish,
    Outcome* out,
    char err_code[32]
);

/**
 * @brief Draws cards for a player
 *
 * @param g             Game state
 * @param player_count  Number of players
 * @param ppos          Player position
 * @param drawn_cards   Output array of drawn cards
 * @param drawn_count   Number of cards drawn
 * @param err_code      Error code buffer
 *
 * @return 1 on success, 0 on failure
 */
int draw(
    Game* g,
    int player_count,
    int ppos,
    unsigned char drawn_cards[MAX_HAND],
    int* drawn_count,
    char err_code[32]
);

#endif
