#include "game.h"
#include <string.h>
#include <stdlib.h>


/** 
 * @brief Returns suit character ('S','H','D','C') for encoded card
 * 
 * @param c Encoded card value
 * 
 * @return Suit character
 */
static char suit_of(unsigned char c) {
    int suit = (int)(c / 8);
    return (suit == 0) ? 'S' : (suit == 1) ? 'H' : (suit == 2) ? 'D' : 'C';
}

/** 
 * @brief Returns rank index 0-7 for encoded card
 * 
 * @param c Encoded card value
 * 
 * @return Rank index
 */
static int rank_of(unsigned char c) {
    return (int)(c % 8);
}

/** 
 * @brief Converts rank index 0-7 to rank character
 * 
 * @param r     Rank index
 * 
 * @return Rank character
 */
static char rank_chr(int r) {
    return (r == 0) ? '7' : (r == 1) ? '8' : (r == 2) ? '9' : (r == 3) ? 'X' : (r == 4) ? 'J' : (r == 5) ? 'Q' : (r == 6) ? 'K' : 'A';
}

/**
 * @brief Converts a rank character to a rank index (0-7)
 *
 * @param ch    Rank character
 *
 * @return Rank index 0-7 on success, -1 if the character is invalid
 */
static int rank_from_chr(char ch) {
    if (ch == '7') return 0;
    if (ch == '8') return 1;
    if (ch == '9') return 2;
    if (ch == 'X') return 3;
    if (ch == 'J') return 4;
    if (ch == 'Q') return 5;
    if (ch == 'K') return 6;
    if (ch == 'A') return 7;

    return -1;
}

/**
 * @brief Converts a suit character to a suit index
 *
 * @param ch    Suit character.
 *
 * @return Suit index 0-3 on success, -1 if the character is invalid
 */
static int suit_from_chr(char ch) {
    if (ch == 'S') return 0;
    if (ch == 'H') return 1;
    if (ch == 'D') return 2;
    if (ch == 'C') return 3;

    return -1;
}

void card_to_str(unsigned char c, char out[4]) {
    out[0] = suit_of(c);
    out[1] = rank_chr(rank_of(c));
    out[2] = '\0';
}

int str_to_card(const char* s, unsigned char* out_card) {
    if (!s || !s[0] || !s[1]) {
        return 0;
    }
    int suit = suit_from_chr(s[0]);
    int rank = rank_from_chr(s[1]);
    if (suit < 0 || rank < 0) {
        return 0;
    }
    *out_card = (unsigned char)(suit * 8 + rank);
    
    return 1;
}

/**
 * @brief Shuffles an array of 32 card bytes in-place 
 *
 * @param a     Array of 32 bytes to shuffle
 * @param seed  Seed used during shuffle
 */
static void shuffle32(unsigned char a[32], unsigned int seed) {
    srand(seed);
    for (int i = 31; i > 0; i--) {
        int j = rand() % (i + 1);
        unsigned char t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

void init(Game* g, int player_count, unsigned int seed) {
    memset(g, 0, sizeof(*g));
    (void)player_count;

    for (int i = 0; i < 32; i++) {
        g->deck[i] = (unsigned char)i;
    }
    shuffle32(g->deck, seed);

    g->deck_top = 0;
    g->discard_top = 0;
    g->penalty = 0;
    g->turn_pos = 0;
    g->running = 1;
    g->ended = 0;
}

/**
 * @brief Draws one card from the deck, refilling from discard pile if needed
 *
 * When the deck is exhausted, this function tries to refill the deck from the discard pile while keeping the current top discard card
 *
 * @param g     Pointer to the game state
 *
 * @return Encoded card (0-31) on success, or 255 if no card can be drawn
 */
static unsigned char draw_one(Game* g) {
    if (g->deck_top >= 32) {
        if (g->discard_top <= 1) {
            return 255;
        }
        unsigned char keep = g->discard[g->discard_top - 1];
        int n = g->discard_top - 1;

        for (int i = 0; i < n; i++) {
            g->deck[i] = g->discard[i];
        }
        g->deck_top = 0;

        srand((unsigned int)rand());
        for (int i = n - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            unsigned char t = g->deck[i];
            g->deck[i] = g->deck[j];
            g->deck[j] = t;
        }

        g->discard[0] = keep;
        g->discard_top = 1;

        for (int i = n; i < 32; i++) g->deck[i] = 255;
    }

    unsigned char c = g->deck[g->deck_top++];
    if (c == 255) {
        return 255;
    }
    return c;
}

void deal(Game* g, int player_count, int cards_each) {
    for (int p = 0; p < player_count; p++) {
        g->hand_count[p] = 0;
        for (int k = 0; k < cards_each; k++) {
            unsigned char c = draw_one(g);
            if (c == 255) {
                break;
            }
            g->hands[p][g->hand_count[p]++] = c;
        }
    }
}

/**
 * @brief Checks whether the card has the specified rank character
 *
 * @param c     Encoded card value (0-31)
 * @param r     Rank character ('7','8','9','X','J','Q','K','A')
 *
 * @return 1 if the card rank matches, 0 otherwise
 */
static int is_rank(unsigned char c, char r) {
    return rank_chr(rank_of(c)) == r;
}

void pick_start_top(Game* g) {
    for (;;) {
        unsigned char c = draw_one(g);
        if (c == 255) {
            break;
        }
        if (is_rank(c, 'Q') || is_rank(c, '7') || is_rank(c, 'A')) {
            g->discard[g->discard_top++] = c;
            continue;
        }
        g->top_card = c;
        g->active_suit = suit_of(c);
        g->discard[g->discard_top++] = c;
        break;
    }
}

int hand_has(const Game* g, int ppos, unsigned char card) {
    for (int i = 0; i < g->hand_count[ppos]; i++) {
        if (g->hands[ppos][i] == card) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Removes one card from a player's hand by swapping with the last card
 *
 * @param g     Pointer to the game state
 * @param ppos  Player position index (0-player_count-1)
 * @param card  Encoded card to remove (0-31)
 */
static void hand_remove(Game* g, int ppos, unsigned char card) {
    for (int i = 0; i < g->hand_count[ppos]; i++) {
        if (g->hands[ppos][i] == card) {
            g->hands[ppos][i] = g->hands[ppos][g->hand_count[ppos]-1];
            g->hand_count[ppos]--;
            return;
        }
    }
}

/**
 * @brief Validates whether a play is legal according to game rules
 *
 * @param g         Pointer to the game state
 * @param card      Encoded card being played
 * @param wish      Suit wish for Queen plays
 * @param err_code  Output buffer for an error code string
 *
 * @return 1 if the play is legal, 0 otherwise.
 */
static int is_play_legal(const Game* g, unsigned char card, const char* wish, char err_code[32]) {
    if (g->penalty > 0) {
        if (!is_rank(card, '7')) {
            strncpy(err_code, "MUST_STACK_OR_DRAW", 31);
            return 0;
        }
        return 1;
    }

    if (is_rank(card, 'Q')) {
        if (!wish || !wish[0]) {
            strncpy(err_code, "WISH_REQUIRED", 31);
            return 0;
        }
        if (!(wish[0]=='S'||wish[0]=='H'||wish[0]=='D'||wish[0]=='C')) {
            strncpy(err_code, "BAD_WISH", 31);
            return 0;
        }
        return 1;
    }

    if (suit_of(card) == g->active_suit) {
        return 1;
    }
    if (rank_of(card) == rank_of(g->top_card)) {
        return 1;
    }

    strncpy(err_code, "ILLEGAL_CARD", 31);
    return 0;
}

void advance_turn(Game* g, int player_count, int skip_next) {
    g->turn_pos = (g->turn_pos + 1) % player_count;
    if (skip_next) {
        g->turn_pos = (g->turn_pos + 1) % player_count;
    }
}

int play(
    Game* g,
    int player_count,
    int ppos,
    unsigned char card,
    const char* wish,
    Outcome* out,
    char err_code[32]
) {
    memset(out, 0, sizeof(*out));
    out->winner_pos = -1;

    if (!g->running || g->ended) {
        strncpy(err_code, "BAD_STATE", 31);
        return 0;
    }
    if (ppos != g->turn_pos) {
        strncpy(err_code, "NOT_YOUR_TURN", 31);
        return 0;
    }
    if (!hand_has(g, ppos, card)) {
        strncpy(err_code, "NO_SUCH_CARD", 31);
        return 0;
    }
    if (!is_play_legal(g, card, wish, err_code)) {
        return 0;
    }

    hand_remove(g, ppos, card);
    g->top_card = card;
    g->discard[g->discard_top++] = card;

    if (is_rank(card, 'Q')) {
        g->active_suit = wish[0];
    }
    else {
        g->active_suit = suit_of(card);
    }

    if (is_rank(card, '7')) {
        g->penalty += 2;
        out->added_penalty = 2;
    }
    if (is_rank(card, 'A')) {
        out->skip_next = 1;
    }

    if (g->hand_count[ppos] == 0) {
        g->ended = 1;
        out->winner_pos = ppos;
        return 1;
    }

    advance_turn(g, player_count, out->skip_next);
    return 1;
}

int draw(
    Game* g,
    int player_count,
    int ppos,
    unsigned char drawn_cards[MAX_HAND],
    int* drawn_count,
    char err_code[32]
) {
    if (!g->running || g->ended) {
        strncpy(err_code, "BAD_STATE", 31);
        return 0;
    }
    if (ppos != g->turn_pos) {
        strncpy(err_code, "NOT_YOUR_TURN", 31);
        return 0;
    }

    int n = (g->penalty > 0) ? g->penalty : 1;
    int got = 0;

    for (int i = 0; i < n; i++) {
        unsigned char c = draw_one(g);
        if (c == 255) {
            break;
        }
        if (g->hand_count[ppos] < MAX_HAND) {
            g->hands[ppos][g->hand_count[ppos]++] = c;
            drawn_cards[got++] = c;
        }
    }

    if (g->penalty > 0) {
        g->penalty = 0;
    }

    *drawn_count = got;

    advance_turn(g, player_count, 0);
    return 1;
}
