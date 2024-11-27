#include "mnk.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

#define  RND_IMPLEMENTATION
#include "rnd.h"

#define MONTE_GAME_CONFIG_TYPE mnk_config_t
#define MONTE_STATE_TYPE mnk_state_t
#define MONTE_MOVE_TYPE mnk_move_t
#define MONTE_RNG_STATE_TYPE rnd_pcg_t
#define MONTE_IMPLEMENTATION
#define MONTE_API static
#define MONTE_USER_FN static
#include "monte.h"

#define NUM_MONTE_THREADS 4

struct mnk_ai_s {
	monte_t* monte[NUM_MONTE_THREADS];
};

static void*
monte_user_alloc(size_t size, size_t alignment, monte_allocator_ctx_t* ctx) {
	return malloc(size);
}

float
monte_user_rng_next(monte_rng_state_t* rng_state) {
	return rnd_pcg_nextf(rng_state);
}

static mnk_state_t*
monte_user_create_state(const mnk_config_t* config) {
	return mnk_state_create(config);
}

static void
monte_user_copy_state(mnk_state_t* dst, const monte_state_t* src) {
	memcpy(dst, src, sizeof(mnk_state_t) + src->config.width * src->config.height);
}

monte_player_id_t
mnk_state_get(const mnk_state_t* state, int8_t x, int8_t y) {
	if (
		   (0 <= x && x < state->config.width)
		&& (0 <= y && y < state->config.height)
	) {
		return state->board[y * state->config.width + x] - 1;
	} else {
		return -1;
	}
}

void
mnk_state_set(mnk_state_t* state, int8_t x, int8_t y, monte_player_id_t player) {
	state->board[y * state->config.width + x] = player + 1;
	--state->num_spaces;
}

static monte_index_t
mnk_count_stride(
	mnk_state_t* state,
	monte_player_id_t player,
	monte_index_t x, monte_index_t y,
	monte_index_t dir_x, monte_index_t dir_y
) {
	monte_index_t stride = 0;

	x += dir_x;
	y += dir_y;
	while (mnk_state_get(state, x, y) == player) {
		++stride;
		x += dir_x;
		y += dir_y;
	}

	return stride;
}

static void
monte_user_apply_move(monte_state_t* state, const monte_move_t* move) {
	if (state->player == MONTE_INVALID_PLAYER) { return; }

	monte_player_id_t player = state->player;
	int8_t x = move->x;
	int8_t y = move->y;
	int8_t stride = state->config.stride - 1;
	mnk_state_set(state, x, y, player);
	if (
		   (mnk_count_stride(state, player, x, y,  1, 0) + mnk_count_stride(state, player, x, y, -1,  0) >= stride)
		|| (mnk_count_stride(state, player, x, y,  0, 1) + mnk_count_stride(state, player, x, y,  0, -1) >= stride)
		|| (mnk_count_stride(state, player, x, y,  1, 1) + mnk_count_stride(state, player, x, y, -1, -1) >= stride)
		|| (mnk_count_stride(state, player, x, y, -1, 1) + mnk_count_stride(state, player, x, y,  1, -1) >= stride)
	) {
		state->player = MONTE_INVALID_PLAYER;
		state->winner = player;
	} else if (state->num_spaces == 0) {
		state->player = MONTE_INVALID_PLAYER;
		state->winner = MONTE_INVALID_PLAYER;
	} else {
		state->player = 1 - state->player;
	}
}

static void
monte_user_inspect_state(const monte_state_t* state, monte_state_info_t* info) {
	if (state->num_spaces == 0) {
		info->current_player = MONTE_INVALID_PLAYER;
		info->scores[0] = 0;
		info->scores[1] = 0;
		return;
	}

	info->current_player = state->player;

	if (state->winner != MONTE_INVALID_PLAYER) {
		info->scores[state->winner] = 1;
		info->scores[1 - state->winner] = -1;
	}
}

static void
monte_user_iterate_moves(const monte_state_t* state, monte_iterator_t* itr) {
	for (int8_t x = 0; x < state->config.width; ++x) {
		for (int8_t y = 0; y < state->config.height; ++y) {
			if (mnk_state_get(state, x, y) == MONTE_INVALID_PLAYER) {
				monte_submit_move(itr, &(mnk_move_t) {
					.x = x,
					.y = y,
				});
			}
		}
	}
}

static bool
monte_user_moves_equal(const monte_move_t* lhs, const monte_move_t* rhs) {
	return (lhs->x == rhs->x) && (lhs->y == rhs->y);
}

static uint64_t
splittable64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9U;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebU;
    x ^= x >> 31;
    return x;
}

static monte_hash_t
monte_user_hash_move(const monte_move_t* move) {
	return splittable64((move->x << 8) | move->x);
}

mnk_state_t*
mnk_state_create(const mnk_config_t* config) {
	mnk_state_t* state = malloc(sizeof(mnk_state_t) + config->width * config->height);
	state->player = 0;
	state->winner = MONTE_INVALID_PLAYER;
	state->num_spaces = config->width * config->height;
	state->config = *config;
	memset(state->board, 0, config->width * config->height);
	return state;
}

void
mnk_state_destroy(mnk_state_t* state) {
	free(state);
}

void
mnk_state_apply(mnk_state_t* state, mnk_move_t move) {
	monte_user_apply_move(state, &move);
}

mnk_ai_t*
mnk_ai_create(const mnk_ai_config_t* config) {
	monte_config_t monte_config = {
		.exploration_param = sqrtf(2.0f),
		.game_config = config->game_config,
		.num_players = 2,
		.state_size = sizeof(mnk_state_t) + config->game_config.width * config->game_config.height,
		.state_alignment = _Alignof(mnk_state_t),
	};
	mnk_ai_t* ai = malloc(sizeof(mnk_ai_t));
	for (int i = 0; i < NUM_MONTE_THREADS; ++i) {
		rnd_pcg_seed(&monte_config.rng_state, i);
		ai->monte[i] = monte_create(config->initial_state, monte_config);
	}
	return ai;
}

void
mnk_ai_destroy(mnk_ai_t* ai) {
	free(ai);
}

static int
mnk_ai_iterate(void* userdata) {
	monte_t* monte = userdata;
	for (int i = 0; i < 150000; ++i) {
		monte_iterate(monte);
	}
	return 0;
}

mnk_move_t
mnk_ai_pick_move(mnk_ai_t* ai) {
	thrd_t threads[NUM_MONTE_THREADS];
	for (int i = 0; i < NUM_MONTE_THREADS; ++i) {
		thrd_create(&threads[i], mnk_ai_iterate, ai->monte[i]);
	}
	for (int i = 0; i < NUM_MONTE_THREADS; ++i) {
		thrd_join(threads[i], NULL);
	}
	monte_index_t best_score = -1;
	mnk_move_t best_move = { 0 };
	for (int i = 0; i < NUM_MONTE_THREADS; ++i) {
		mnk_move_t move;
		float score;
		monte_pick_move(ai->monte[i], &move, &score);
		printf("%d %d %d\n", best_move.x, best_move.y, best_score);
		if (score > best_score) {
			best_move = move;
			best_score = score;
		}
	}

	return best_move;
}

void
mnk_ai_apply(mnk_ai_t* ai, mnk_move_t move) {
	for (int i = 0; i < NUM_MONTE_THREADS; ++i) {
		monte_apply_move(ai->monte[i], &move);
	}
}
