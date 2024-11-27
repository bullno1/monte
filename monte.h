#ifndef MONTE_H
#define MONTE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef MONTE_API
#	define MONTE_API
#endif

#ifndef MONTE_INDEX_TYPE
#	define MONTE_INDEX_TYPE int32_t
#endif

#ifndef MONTE_PLAYER_ID_TYPE
#	define MONTE_PLAYER_ID_TYPE int8_t
#endif

#ifndef MONTE_STATE_TYPE
#	define MONTE_STATE_TYPE struct { int32_t internal; }
#endif

#ifndef MONTE_GAME_CONFIG_TYPE
#	define MONTE_GAME_CONFIG_TYPE struct { int32_t internal; }
#endif

#ifndef MONTE_MOVE_TYPE
#	define MONTE_MOVE_TYPE struct { int32_t internal; }
#endif

#ifndef MONTE_ALLOCATOR_CTX_TYPE
#	define MONTE_ALLOCATOR_CTX_TYPE void
#endif

#ifndef MONTE_RNG_STATE_TYPE
#	define MONTE_RNG_STATE_TYPE int32_t
#endif

#ifndef MONTE_USER_FN
#	define MONTE_USER_FN extern
#endif

#define MONTE_INVALID_PLAYER ((MONTE_INDEX_TYPE)-1)

typedef MONTE_INDEX_TYPE monte_index_t;
typedef MONTE_PLAYER_ID_TYPE monte_player_id_t;
typedef MONTE_GAME_CONFIG_TYPE monte_game_config_t;
typedef MONTE_STATE_TYPE monte_state_t;
typedef MONTE_MOVE_TYPE monte_move_t;
typedef MONTE_ALLOCATOR_CTX_TYPE monte_allocator_ctx_t;
typedef MONTE_RNG_STATE_TYPE monte_rng_state_t;
typedef uint64_t monte_hash_t;
typedef struct monte_s monte_t;

typedef struct monte_state_info_s {
	monte_player_id_t current_player;
	monte_index_t scores[];
} monte_state_info_t;

typedef struct monte_iterator_s monte_iterator_t;

typedef struct monte_config_s {
	monte_player_id_t num_players;
	size_t state_size;
	size_t state_alignment;
	float exploration_param;
	monte_game_config_t game_config;

	monte_allocator_ctx_t* allocator_ctx;
	monte_rng_state_t rng_state;
} monte_config_t;

// User-provided functions

MONTE_USER_FN void*
monte_user_alloc(size_t size, size_t alignment, monte_allocator_ctx_t* ctx);

MONTE_USER_FN float
monte_user_rng_next(monte_rng_state_t* rng_state);

MONTE_USER_FN monte_state_t*
monte_user_create_state(const monte_game_config_t* config);

MONTE_USER_FN void
monte_user_copy_state(monte_state_t* dst, const monte_state_t* src);

MONTE_USER_FN void
monte_user_apply_move(monte_state_t* state, const monte_move_t* move);

MONTE_USER_FN void
monte_user_inspect_state(const monte_state_t* state, monte_state_info_t* info);

MONTE_USER_FN void
monte_user_iterate_moves(const monte_state_t* state, monte_iterator_t* iterator);

MONTE_USER_FN bool
monte_user_moves_equal(const monte_move_t* lhs, const monte_move_t* rhs);

MONTE_USER_FN monte_hash_t
monte_user_hash_move(const monte_move_t* move);

// API

MONTE_API monte_t*
monte_create(const monte_state_t* initial_state, monte_config_t config);

MONTE_API void
monte_apply_move(monte_t* monte, const monte_move_t* move);

MONTE_API void
monte_iterate(monte_t* monte);

MONTE_API void
monte_pick_move(monte_t* monte, monte_move_t* move);

MONTE_API void
monte_submit_move(monte_iterator_t* itr, const monte_move_t* move);

#endif

#ifdef MONTE_IMPLEMENTATION

#include <math.h>
#include <string.h>

#ifndef MONTE_HAMT_NUMBITS
#	define MONTE_HAMT_NUM_BITS 2
#endif

#define MONTE_HAMT_NUM_CHILDREN (1 << MONTE_HAMT_NUM_BITS)
#define MONTE_HAMT_MASK (((monte_hash_t)1 << MONTE_HAMT_NUM_BITS) - 1)

typedef struct monte_node_s monte_node_t;

struct monte_node_s {
	monte_node_t* hamt[MONTE_HAMT_NUM_CHILDREN];
	monte_move_t move;
	monte_index_t num_moves_left;
	monte_node_t* next;
	monte_node_t* children;
	monte_player_id_t instant_winner;

	monte_node_t* parent;
	monte_player_id_t current_player;
	monte_index_t num_wins;
	monte_index_t num_visits;
};

struct monte_s {
	monte_config_t config;
	monte_node_t* node_pool;

	monte_state_t* current_state;
	monte_state_t* tmp_state;
	monte_state_t* tmp_state2;
	monte_state_info_t* tmp_state_info;
	monte_node_t* root;
};

typedef void (*monte_submit_move_fn_t)(void* userdata, const monte_move_t* move);

struct monte_iterator_s {
	monte_submit_move_fn_t fn;
	void* userdata;
};

typedef struct {
	monte_index_t num_moves;
	monte_move_t move;
	monte_rng_state_t* rng_state;
	monte_node_t** in_node;
	monte_node_t** out_node;
} monte_iterator_for_expansion_t;

typedef struct {
	monte_index_t num_moves;
	monte_move_t move;
	monte_rng_state_t* rng_state;

	bool found_decisive;
	const monte_state_t* current_state;
	monte_state_t* tmp_state;
	monte_state_info_t* state_info;
} monte_iterator_for_simulation_t;

static inline monte_node_t*
monte_alloc_node(monte_t* monte) {
	monte_node_t* node = monte->node_pool;
	if (node != NULL) {
		monte->node_pool = node->next;
		return node;
	} else {
		return monte_user_alloc(
			sizeof(monte_node_t),
			_Alignof(monte_node_t),
			monte->config.allocator_ctx
		);
	}
}

static inline void
monte_free_node(monte_node_t* node, monte_t* monte) {
	node->next = monte->node_pool;
	monte->node_pool = node;
}

static inline monte_node_t**
monte_find_node(monte_node_t** root, const monte_move_t* move) {
	monte_node_t** node_itr = root;
	monte_hash_t hash_itr = monte_user_hash_move(move);
	for (
		;
		*node_itr != NULL;
		hash_itr >>= MONTE_HAMT_NUM_BITS
	) {
		monte_node_t* node = *node_itr;
		if (monte_user_moves_equal(&node->move, move)) {
			return node_itr;
		}
		node_itr = &node->hamt[hash_itr & MONTE_HAMT_MASK];
	}

	return node_itr;
}

static inline void
monte_iterate_moves(const monte_state_t* state, monte_submit_move_fn_t fn, void* userdata) {
	monte_iterator_t itr = {
		.fn = fn,
		.userdata = userdata,
	};
	monte_user_iterate_moves(state, &itr);
}

static inline void
monte_submit_move_for_expansion(void* userdata, const monte_move_t* move) {
	monte_iterator_for_expansion_t* itr = userdata;
	monte_node_t** move_ptr = monte_find_node(itr->in_node, move);
	if (*move_ptr != NULL) { return; }

	bool move_chosen = false;
	if (itr->num_moves == 0) {
		itr->move = *move;
		++itr->num_moves;
		move_chosen = true;
	} else {
		monte_index_t num_moves = ++itr->num_moves;
		float random_number = monte_user_rng_next(itr->rng_state);
		if ((random_number * (float)num_moves) < 1.f) {
			itr->move = *move;
			move_chosen = true;
		}
	}

	if (move_chosen) {
		itr->out_node = move_ptr;
	}
}

static inline monte_iterator_for_expansion_t
monte_iterate_moves_for_expansion(const monte_state_t* state, monte_node_t* node, monte_t* monte) {
	monte_iterator_for_expansion_t itr = {
		.rng_state = &monte->config.rng_state,
		.in_node = node != NULL ? &node->children : NULL,
	};
	monte_iterate_moves(state, monte_submit_move_for_expansion, &itr);
	return itr;
}

static inline void
monte_submit_move_for_simulation(void* userdata, const monte_move_t* move) {
	monte_iterator_for_simulation_t* itr = userdata;

	// If we have found the decisive move, there is no need to continue
	if (itr->found_decisive) { return; }

	// Check whether the submitted move immediately end the game in the current
	// player's favor.
	//monte_state_t* state = itr->tmp_state;
	//monte_user_copy_state(state, itr->current_state);
	//monte_state_info_t* state_info = itr->state_info;
	//monte_user_inspect_state(state, state_info);
	//monte_player_id_t player = state_info->current_player;

	//monte_user_apply_move(state, move);
	//monte_user_inspect_state(state, state_info);
	//if (
		//state_info->current_player == MONTE_INVALID_PLAYER
		//&& state_info->scores[player] > 0
	//) {
		//itr->found_decisive = true;
		//itr->move = *move;
		//return;
	//}

	if (itr->num_moves == 0) {
		itr->move = *move;
		++itr->num_moves;
	} else {
		monte_index_t num_moves = ++itr->num_moves;
		float random_number = monte_user_rng_next(itr->rng_state);
		if ((random_number * (float)num_moves) < 1.f) {
			itr->move = *move;
		}
	}
}

static inline monte_move_t
monte_pick_move_for_simulation(const monte_state_t* state, monte_t* monte) {
	monte_iterator_for_simulation_t itr = {
		.rng_state = &monte->config.rng_state,
		.current_state = state,
		.tmp_state = monte->tmp_state2,
		.state_info = monte->tmp_state_info,
	};
	monte_iterate_moves(state, monte_submit_move_for_simulation, &itr);
	return itr.move;
}

monte_t*
monte_create(const monte_state_t* initial_state, monte_config_t config) {
	monte_t* monte = monte_user_alloc(sizeof(monte_t), _Alignof(monte_t), config.allocator_ctx);
	*monte = (monte_t){
		.config = config,
		.current_state = monte_user_create_state(&config.game_config),
		.tmp_state = monte_user_create_state(&config.game_config),
		.tmp_state2 = monte_user_create_state(&config.game_config),
		.tmp_state_info = monte_user_alloc(
			sizeof(monte_state_info_t) + sizeof(monte_index_t) * config.num_players,
			_Alignof(monte_state_info_t),
			config.allocator_ctx
		),
	};

	monte_node_t* root = monte_alloc_node(monte);
	*root = (monte_node_t) {
		.num_moves_left = -1,
	};
	monte->root = root;

	monte_user_copy_state(monte->current_state, initial_state);

	monte_user_inspect_state(monte->current_state, monte->tmp_state_info);
	root->current_player = monte->tmp_state_info->current_player;

	return monte;
}

void
monte_iterate(monte_t* monte) {
	monte_state_t* state = monte->tmp_state;
	monte_user_copy_state(state, monte->current_state);

	// Selection
	monte_node_t* node = monte->root;
	{
		float c = monte->config.exploration_param;
		monte_player_id_t player = node->current_player;
		while (node->num_moves_left == 0) {
			float chosen_uct_score = -INFINITY;
			monte_node_t* chosen_node = NULL;
			float parent_log_n = logf((float)node->num_visits);
			for (
				monte_node_t* itr = node->children;
				itr != NULL;
				itr = itr->next
			) {
				if (itr->instant_winner == player) {
					chosen_node = itr;
					break;
				}

				float win_rate = (float)itr->num_wins / (float)itr->num_visits;
				float explore_rate = c * sqrtf(parent_log_n / (float)itr->num_visits);
				float uct_score = win_rate + explore_rate;
				if (uct_score > chosen_uct_score) {
					chosen_uct_score = uct_score;
					chosen_node = itr;
				}
			}

			if (chosen_node == NULL) { break; }

			monte_user_apply_move(state, &chosen_node->move);
			node = chosen_node;
		}
	}

	// Expansion
	monte_state_info_t* state_info = monte->tmp_state_info;
	monte_user_inspect_state(state, state_info);
	if (state_info->current_player != MONTE_INVALID_PLAYER) {
		monte_iterator_for_expansion_t itr = monte_iterate_moves_for_expansion(state, node, monte);
		node->num_moves_left = itr.num_moves - 1;

		if (itr.num_moves > 0) {
			monte_node_t* head = node->children;
			monte_node_t* new_node = *itr.out_node = monte_alloc_node(monte);

			monte_user_apply_move(state, &itr.move);
			monte_user_inspect_state(state, state_info);

			*new_node = (monte_node_t) {
				.move = itr.move,
				.num_moves_left = -1,  // Unknown
				.parent = node,
				.current_player = state_info->current_player,
				.instant_winner = MONTE_INVALID_PLAYER,
			};
			if (state_info->current_player == MONTE_INVALID_PLAYER) {
				for (
					monte_player_id_t player_index = 0;
					player_index < monte->config.num_players;
					++player_index
				) {
					if (state_info->scores[player_index] > 0) {
						new_node->instant_winner = player_index;
						break;
					}
				}
			}

			if (head != NULL) {
				new_node->next = head->next;
				head->next = new_node;
			}

			node = new_node;
		}
	}

	// Simulation
	while (state_info->current_player != MONTE_INVALID_PLAYER) {
		monte_move_t move = monte_pick_move_for_simulation(state, monte);
		monte_user_apply_move(state, &move);
		monte_user_inspect_state(state, state_info);
	}

	// Backpropagation
	while (node->parent != NULL) {
		monte_player_id_t player = node->parent->current_player;
		node->num_visits += 1;
		node->num_wins += state_info->scores[player];

		node = node->parent;
	}
	monte->root->num_visits += 1;
}

void
monte_pick_move(monte_t* monte, monte_move_t* move) {
	monte_index_t num_visits = -1;
	for (
		monte_node_t* itr = monte->root->children;
		itr != NULL;
		itr = itr->next
	) {
		if (itr->num_visits > num_visits) {
			*move = itr->move;
			num_visits = itr->num_visits;
		}
	}
}

void
monte_submit_move(monte_iterator_t* itr, const monte_move_t* move) {
	itr->fn(itr->userdata, move);
}

void
monte_apply_move(monte_t* monte, const monte_move_t* move) {
	monte_node_t* new_root = NULL;
	monte_node_t* recycle_root = NULL;
	for (
		monte_node_t* itr = monte->root->children;
		itr != NULL;
	) {
		monte_node_t* next = itr->next;

		if (monte_user_moves_equal(&itr->move, move)) {
			new_root = itr;
		} else {
			itr->next = recycle_root;
			recycle_root = itr;
		}

		itr = next;
	}

	while (recycle_root != NULL) {
		monte_node_t* node = recycle_root;
		recycle_root = node->next;

		for (
			monte_node_t* itr = node->children;
			itr != NULL;
		) {
			monte_node_t* itr_next = itr->next;
			itr->next = recycle_root;
			recycle_root = itr;
			itr = itr_next;
		}

		monte_free_node(node, monte);
	}

	monte_user_apply_move(monte->current_state, move);

	if (new_root == NULL) {
		new_root = monte_alloc_node(monte);
		*new_root = (monte_node_t){
			.num_moves_left = -1,
		};
		monte_user_inspect_state(monte->current_state, monte->tmp_state_info);
		new_root->current_player = monte->tmp_state_info->current_player;
	}

	new_root->parent = NULL;
	monte->root = new_root;
}

#endif
