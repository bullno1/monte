#ifndef MONTE_MNK_H
#define MONTE_MNK_H

#include <stdint.h>

typedef struct mnk_state_s mnk_state_t;
typedef struct mnk_config_s mnk_config_t;
typedef struct mnk_move_s mnk_move_t;
typedef struct mnk_ai_config_s mnk_ai_config_t;
typedef struct mnk_ai_s mnk_ai_t;

struct mnk_config_s {
	int8_t width;
	int8_t height;
	int8_t stride;
};

struct mnk_state_s {
	mnk_config_t config;

	int8_t player;
	int8_t winner;
	int16_t num_spaces;
	int8_t board[];
};

struct mnk_move_s {
	int8_t x;
	int8_t y;
};

struct mnk_ai_config_s {
	mnk_config_t game_config;
	mnk_state_t* initial_state;
};

mnk_state_t*
mnk_state_create(const mnk_config_t* config);

void
mnk_state_destroy(mnk_state_t* state);

void
mnk_state_apply(mnk_state_t* state, mnk_move_t move);

mnk_ai_t*
mnk_ai_create(const mnk_ai_config_t* config);

void
mnk_ai_destroy(mnk_ai_t* ai);

mnk_move_t
mnk_ai_pick_move(mnk_ai_t* ai);

void
mnk_ai_apply(mnk_ai_t* ai, mnk_move_t move);

int8_t
mnk_state_get(const mnk_state_t* state, int8_t x, int8_t y);

#endif
