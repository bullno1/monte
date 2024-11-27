#include "mnk.h"
#include <stdio.h>
#include <stdbool.h>

static void
print_state(const mnk_state_t* state, mnk_move_t move) {
	for (int8_t y = 0; y < state->config.height; ++y) {
		for (int8_t x = 0; x < state->config.width; ++x) {
			int8_t value = mnk_state_get(state, x, y);

			bool is_latest_move = x == move.x && y == move.y;
			char c;
			if (value == -1) {
				c = '_';
			} else if (value == 0) {
				c = is_latest_move ? '+' : 'x';
			} else {
				c = is_latest_move ? '0' : 'o';
			}

			printf(" %c", c);
		}
		printf("\n");
	}
}

static inline void
load_state(mnk_state_t* mnk, const char* state[]) {
	for (int8_t y = 0; y < mnk->config.height; ++y) {
		for (int8_t x = 0; x < mnk->config.width; ++x) {
			char c = state[y][x];
			switch (c) {
				case '_':
					break;
				case '+':
					mnk->player = 1;
				case 'x':
					mnk_state_set(mnk, x, y, 0);
					break;
				case '0':
					mnk->player = 0;
				case 'o':
					mnk_state_set(mnk, x, y, 1);
					break;
			}
		}
	}
}

int main(int argc, const char* argv[]) {
	mnk_config_t config = {
		.width =  9,
		.height = 9,
		.stride = 5,
	};
	mnk_state_t* mnk = mnk_state_create(&config);
	const char* state[] = {
		"_________",
		"_________",
		"x___o_x__",
		"_oo_xo___",
		"__oxox___",
		"__xoxx+__",
		"xoooox___",
		"_x___x___",
		"_____o___",
	};
	load_state(mnk, state);

	mnk_ai_config_t ai_config = {
		.game_config = config,
		.initial_state = mnk,
	};
	mnk_ai_t* ai = mnk_ai_create(&ai_config);

	(void)ai;

	print_state(mnk, (mnk_move_t) { -1, -1 });

	while (mnk->player != -1) {
		mnk_move_t move = mnk_ai_pick_move(ai);
		printf("Move: %d %d\n", move.x, move.y);
		mnk_state_apply(mnk, move);
		print_state(mnk, move);
		mnk_ai_apply(ai, move);
	}
	printf("Winner: %d\n", mnk->winner);

	return 0;
}
