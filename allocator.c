#include "allocator.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

QCGC_STATIC size_t bytes_to_cells(size_t bytes);

QCGC_STATIC void bump_allocator_assign(cell_t *ptr, size_t cells);
QCGC_STATIC cell_t *bump_allocator_allocate(size_t cells);
QCGC_STATIC void bump_allocator_advance(size_t cells);

QCGC_STATIC bool is_small(size_t cells);
QCGC_STATIC size_t small_index(size_t cells);
QCGC_STATIC size_t large_index(size_t cells);
QCGC_STATIC size_t small_index_to_cells(size_t index);

QCGC_STATIC cell_t *fit_allocator_allocate(size_t cells);
QCGC_STATIC cell_t *fit_allocator_small_first_fit(size_t index, size_t cells);
QCGC_STATIC cell_t *fit_allocator_large_first_fit(size_t index, size_t cells);

QCGC_STATIC bool valid_block(cell_t *ptr, size_t cells);

void qcgc_allocator_initialize(void) {
	qcgc_allocator_state.arenas =
		qcgc_arena_bag_create(QCGC_ARENA_BAG_INIT_SIZE);

	// Bump Allocator
	qcgc_allocator_state.bump_state.bump_ptr = NULL;
	qcgc_allocator_state.bump_state.remaining_cells = 0;

	// Fit Allocator
	for (size_t i = 0; i < QCGC_SMALL_FREE_LISTS; i++) {
		qcgc_allocator_state.fit_state.small_free_list[i] =
			qcgc_linear_free_list_create(QCGC_SMALL_FREE_LIST_INIT_SIZE);
	}

	for (size_t i = 0; i < QCGC_LARGE_FREE_LISTS; i++) {
		qcgc_allocator_state.fit_state.large_free_list[i] =
			qcgc_exp_free_list_create(QCGC_LARGE_FREE_LIST_INIT_SIZE);
	}
}

void qcgc_allocator_destroy(void) {
	// Fit Allocator
	for (size_t i = 0; i < QCGC_SMALL_FREE_LISTS; i++) {
		free(qcgc_allocator_state.fit_state.small_free_list[i]);
	}

	for (size_t i = 0; i < QCGC_LARGE_FREE_LISTS; i++) {
		free(qcgc_allocator_state.fit_state.large_free_list[i]);
	}

	// Arenas
	size_t arena_count = qcgc_allocator_state.arenas->count;
	for (size_t i = 0; i < arena_count; i++) {
		qcgc_arena_destroy(qcgc_allocator_state.arenas->items[i]);
	}

	free(qcgc_allocator_state.arenas);
}

cell_t *qcgc_allocator_allocate(size_t bytes) {
	size_t size_in_cells = bytes_to_cells(bytes);
#if CHECKED
	assert(size_in_cells > 0);
	assert(size_in_cells <= QCGC_ARENA_CELLS_COUNT - QCGC_ARENA_FIRST_CELL_INDEX);
#endif
	cell_t *result;

	// TODO: Implement switch for bump/fit allocator
	if (true) {
		result = bump_allocator_allocate(size_in_cells);
	} else {
		result = fit_allocator_allocate(size_in_cells);
	}

	qcgc_arena_mark_allocated(result, size_in_cells);
#if QCGC_INIT_ZERO
	memset(result, 0, bytes);
#endif
	return result;
}

void qcgc_fit_allocator_add(cell_t *ptr, size_t cells) {
	if (cells > 0) {
		if (is_small(cells)) {
			size_t index = small_index(cells);
			qcgc_allocator_state.fit_state.small_free_list[index] =
				qcgc_linear_free_list_add(
						qcgc_allocator_state.fit_state.small_free_list[index],
						ptr);
		} else {
			size_t index = large_index(cells);
			qcgc_allocator_state.fit_state.large_free_list[index] =
				qcgc_exp_free_list_add(
						qcgc_allocator_state.fit_state.large_free_list[index],
						(struct exp_free_list_item_s) {ptr, cells});
		}
	}
}

QCGC_STATIC void bump_allocator_assign(cell_t *ptr, size_t cells) {
	qcgc_allocator_state.bump_state.bump_ptr = ptr;
	qcgc_allocator_state.bump_state.remaining_cells = cells;
}

QCGC_STATIC void bump_allocator_advance(size_t cells) {
	qcgc_allocator_state.bump_state.bump_ptr += cells;
	qcgc_allocator_state.bump_state.remaining_cells -= cells;
}

QCGC_STATIC cell_t *bump_allocator_allocate(size_t cells) {
	if (cells > qcgc_allocator_state.bump_state.remaining_cells) {
		// Grab a new arena
		arena_t *arena = qcgc_arena_create();
		bump_allocator_assign(&(arena->cells[QCGC_ARENA_FIRST_CELL_INDEX]),
				QCGC_ARENA_CELLS_COUNT - QCGC_ARENA_FIRST_CELL_INDEX);
		qcgc_allocator_state.arenas =
			qcgc_arena_bag_add(qcgc_allocator_state.arenas, arena);
	}
	cell_t *result = qcgc_allocator_state.bump_state.bump_ptr;
	bump_allocator_advance(cells);
	return result;
}

QCGC_STATIC cell_t *fit_allocator_allocate(size_t cells) {
	cell_t *result;

	if (is_small(cells)) {
		size_t index = small_index(cells);
		result = fit_allocator_small_first_fit(index, cells);
	} else {
		size_t index = large_index(cells);
		result = fit_allocator_large_first_fit(index, cells);
	}

	if (result == NULL) {
		// No valid block found
		result = bump_allocator_allocate(cells);
	}

	return result;
}

QCGC_STATIC cell_t *fit_allocator_small_first_fit(size_t index, size_t cells) {
	cell_t *result = NULL;
	for ( ; index < QCGC_SMALL_FREE_LISTS; index++) {
		linear_free_list_t *free_list =
			qcgc_allocator_state.fit_state.small_free_list[index];
		size_t list_cell_size = small_index_to_cells(index);

		while (free_list->count > 0) {
			result = free_list->items[free_list->count - 1];
			free_list = qcgc_linear_free_list_remove_index(free_list,
					free_list->count - 1);

			// Check whether block is still valid
			if (valid_block(result, list_cell_size)) {
				qcgc_fit_allocator_add(result + cells, list_cell_size - cells);
				break;
			} else {
				result = NULL;
			}
		}

		qcgc_allocator_state.fit_state.small_free_list[index] = free_list;
		if (result != NULL) {
			return result;
		}
	}
	return fit_allocator_large_first_fit(0, cells);
}

QCGC_STATIC cell_t *fit_allocator_large_first_fit(size_t index, size_t cells) {
	cell_t *result = NULL;
	for ( ; index < QCGC_LARGE_FREE_LISTS; index++) {
		exp_free_list_t *free_list =
			qcgc_allocator_state.fit_state.large_free_list[index];
		while(free_list->count > 0) {
			struct exp_free_list_item_s item =
				free_list->items[free_list->count - 1];
			free_list = qcgc_exp_free_list_remove_index(free_list,
					free_list->count - 1);

			// Check whether block is still valid
			if (valid_block(item.ptr, item.size)) {
				qcgc_fit_allocator_add(item.ptr + item.size, item.size - cells);
				result = item.ptr;
				break;
			}
		}
		qcgc_allocator_state.fit_state.large_free_list[index] = free_list;
		if (result != NULL) {
			return result;
		}
	}
	return NULL;
}

QCGC_STATIC size_t bytes_to_cells(size_t bytes) {
	return (bytes + sizeof(cell_t) - 1) / sizeof(cell_t);
}

QCGC_STATIC bool is_small(size_t cells) {
	return cells <= QCGC_SMALL_FREE_LISTS;
}

QCGC_STATIC size_t small_index(size_t cells) {
#if CHECKED
	assert(is_small(cells));
#endif
	return cells - 1;
}

QCGC_STATIC size_t large_index(size_t cells) {
#if CHECKED
	assert(!is_small(cells));
#endif
	// shift such that the meaningless part disappears, i.e. everything that
	// belongs into the first free list will become 1.
	cells = cells >> QCGC_LARGE_FREE_LIST_FIRST_EXP;

	// calculates floor(log(cells))
	return (8 * sizeof(unsigned long)) - __builtin_clzl(cells) - 1;
}

QCGC_STATIC size_t small_index_to_cells(size_t index) {
#if CHECKED
	assert(index < QCGC_SMALL_FREE_LISTS);
#endif
	return index + 1;
}

QCGC_STATIC bool valid_block(cell_t *ptr, size_t cells) {
#if CHECKED
	assert(ptr != NULL);
	assert(cells > 0);
#endif
	return (qcgc_arena_get_blocktype(ptr) == BLOCK_FREE &&
			qcgc_arena_get_blocktype(ptr + cells) != BLOCK_EXTENT);
}
