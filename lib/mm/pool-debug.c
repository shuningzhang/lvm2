/*
 * Copyright (C) 2001 Sistina Software (UK) Limited.
 *
 * This file is released under the LGPL.
 */

#include "pool.h"
#include "dbg_malloc.h"
#include "log.h"

#include <assert.h>

struct block {
	struct block *next;
	char data[0];
};

struct pool {
	struct block *blocks;
	struct block *tail;
};

/* by default things come out aligned for doubles */
#define DEFAULT_ALIGNMENT __alignof__ (double)

struct pool *pool_create(size_t chunk_hint)
{
	struct pool *mem = dbg_malloc(sizeof(*mem));

	if (!mem) {
		stack;
		return NULL;
	}

	mem->blocks = mem->tail = NULL;
	return mem;
}

static void _free_blocks(struct block *b)
{
	struct block *n;

	while (b) {
		n = b->next;
		dbg_free(b);
		b = n;
	}
}

void pool_destroy(struct pool *p)
{
	_free_blocks(p->blocks);
	dbg_free(p);
}

void *pool_alloc(struct pool *p, size_t s)
{
	return pool_alloc_aligned(p, s, DEFAULT_ALIGNMENT);
}

void *pool_alloc_aligned(struct pool *p, size_t s, unsigned alignment)
{
	/* FIXME: I'm currently ignoring the alignment arg. */
	size_t len = sizeof(struct block) + s;
	struct block *b = dbg_malloc(len);

	if (!b) {
		stack;
		return NULL;
	}

	b->next = NULL;

	if (p->tail) {
		p->tail->next = b;
		p->tail = b;
	} else
		p->blocks = p->tail = b;

	return &b->data[0];
}

void pool_empty(struct pool *p)
{
	_free_blocks(p->blocks);
	p->blocks = p->tail = NULL;
}

void pool_free(struct pool *p, void *ptr)
{
	struct block *b, *prev = NULL;

	for (b = p->blocks; b; b = b->next) {
		if ((void *) &b->data[0] == ptr)
			break;
		prev = b;
	}

	/*
	 * If this fires then you tried to free a
	 * pointer that either wasn't from this
	 * pool, or isn't the start of a block.
	 */
	assert(b);

	_free_blocks(b);

	if (prev) {
		p->tail = prev;
		prev->next = NULL;
	} else
		p->blocks = p->tail = NULL;
}

void *pool_begin_object(struct pool *p, size_t hint, unsigned align)
{
	log_err("pool_begin_object not implemented for pool-debug");
	assert(0);
}

void *pool_grow_object(struct pool *p, unsigned char *buffer, size_t n)
{
	log_err("pool_grow_object not implemented for pool-debug");
	assert(0);
}

void *pool_end_object(struct pool *p)
{
	log_err("pool_end_object not implemented for pool-debug");
	assert(0);
}

void pool_abandon_object(struct pool *p)
{
	log_err("pool_abandon_object not implemented for pool-debug");
}

char *pool_strdup(struct pool *p, const char *str)
{
	char *ret = pool_alloc(p, strlen(str) + 1);

	if (ret)
		strcpy(ret, str);

	return ret;
}
