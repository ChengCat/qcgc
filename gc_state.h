#pragma once

#include <stddef.h>

#include "shadow_stack.h"

/**
 * @typedef gc_state_t
 * Garbage collection states.
 * - GC_PAUSE	No gc in progress
 * - GC_MARK	Currently marking
 * - GC_COLLECT	Currently collecting
 */
typedef enum gc_phase {
	GC_PAUSE,
	GC_MARK,
	GC_COLLECT,
} gc_phase_t;

/**
 * @var qcgc_state
 *
 * Global state of the garbage collector
 */
struct qcgc_state {
	shadow_stack_t *shadow_stack;
	shadow_stack_t *prebuilt_objects;
	gray_stack_t *gp_gray_stack;
	size_t gray_stack_size;
	gc_phase_t phase;
	size_t bytes_since_collection;
} qcgc_state;
