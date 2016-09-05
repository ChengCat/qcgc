#pragma once

#include <stddef.h>

#include "bag.h"
#include "gray_stack.h"
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
	shadow_stack_t *extra_roots;	// Additional roots about whose structure we
									// can't assume anything
									// XXX: Wrong item type (object_t * instead
									// of object_t **)
	weakref_bag_t *weakrefs;
	gray_stack_t *gp_gray_stack;
	size_t gray_stack_size;
	gc_phase_t phase;
	size_t bytes_since_collection;
	size_t bytes_since_incmark;
} qcgc_state;
