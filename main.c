#include "mnk.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void
print_state(const mnk_state_t* state) {
	for (int8_t y = 0; y < state->config.height; ++y) {
		for (int8_t x = 0; x < state->config.width; ++x) {
			int8_t value = mnk_state_get(state, x, y);

			char c;
			if (value == -1) {
				c = '_';
			} else if (value == 0) {
				c = 'x';
			} else {
				c = 'o';
			}

			printf(" %c", c);
		}
		printf("\n");
	}
}

int main(int argc, const char* argv[]) {
	mnk_config_t config = {
		.width =  9,
		.height = 9,
		.stride = 5,
	};
	mnk_state_t* mnk = mnk_state_create(&config);

	mnk_ai_config_t ai_config = {
		.game_config = config,
		.initial_state = mnk,
	};
	mnk_ai_t* ai = mnk_ai_create(&ai_config);

	(void)ai;

	print_state(mnk);

	while (mnk->player != -1) {
		mnk_move_t move = mnk_ai_pick_move(ai);
		printf("Move: %d %d\n", move.x, move.y);
		mnk_state_apply(mnk, move);
		print_state(mnk);
		mnk_ai_apply(ai, move);
	}
	printf("Winner: %d\n", mnk->winner);

	return 0;
}
