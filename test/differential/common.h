/*
 * Copyright (c) 2026, ArtGins.
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

/*
 * What the programs of the differential test share (see README.md):
 *
 * - a seeded random generator, so that two builds of jansson see the same
 *   cases;
 * - the output of a case: one line per case, printed whole with -v, or
 *   folded into a digest printed at the end, which is what run.sh compares
 *   between two builds;
 * - an allocator that counts what jansson asks for and can refuse blocks
 *   over a size (what a custom allocator sees is not part of what two
 *   builds must agree on: its lines start with "alloc", and run.sh leaves
 *   them out unless asked).
 */

#ifndef DIFFERENTIAL_COMMON_H
#define DIFFERENTIAL_COMMON_H

#include <jansson.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  The random generator: xorshift64, the same sequence everywhere
 */
static uint64_t rnd_state = 1;

static inline void rnd_seed(uint64_t seed) { rnd_state = seed ? seed : 1; }

static inline uint32_t rnd(void) {
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 7;
    rnd_state ^= rnd_state << 17;
    return (uint32_t)rnd_state;
}

/*
 *  The output: a line per case, or a digest of the lines (FNV-1a, 64 bits)
 */
static int out_verbose = 0;
static uint64_t out_digest = 1469598103934665603ULL;
static unsigned long out_cases = 0;

static inline void out_fold(const char *data, size_t len) {
    size_t i;

    for (i = 0; i < len; i++) {
        out_digest ^= (unsigned char)data[i];
        out_digest *= 1099511628211ULL;
    }
}

/* Bytes of a case, printed with the escapes of C when they are not plain */
static inline void out_bytes(const char *data, size_t len) {
    size_t i;

    if (!out_verbose) {
        out_fold(data, len);
        return;
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\\') {
            fputs("\\\\", stdout);
        } else if (c < 0x20 || c >= 0x7f) {
            printf("\\x%02x", c);
        } else {
            putchar(c);
        }
    }
}

static inline void out_text(const char *fmt, ...) {
    char line[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) {
        fprintf(stderr, "out_text(): vsnprintf() failed\n");
        exit(2);
    }
    if ((size_t)n >= sizeof(line)) {
        n = (int)sizeof(line) - 1;
    }
    out_bytes(line, (size_t)n);
}

/* The end of the line of a case */
static inline void out_end(void) {
    out_cases++;
    if (out_verbose) {
        putchar('\n');
    } else {
        out_fold("\n", 1);
    }
}

/* The digest of a program, when it does not print every line */
static inline void out_finish(const char *program) {
    if (!out_verbose) {
        printf("%s: %lu cases, digest %016llx\n", program, out_cases,
               (unsigned long long)out_digest);
    }
}

/* A dump of json under flags, as a case: its text, or that it failed */
static inline void out_dump(const char *label, const json_t *json, size_t flags) {
    char *text = json_dumps(json, flags);

    out_text("%s %zx ", label, flags);
    if (text) {
        out_bytes(text, strlen(text));
        free(text);
    } else {
        out_text("NULL");
    }
    out_end();
}

/* The error of a load, as a case */
static inline void out_error(const char *label, const json_error_t *error) {
    out_text("%s error %d line %d column %d position %d source %s: ", label,
             (int)json_error_code(error), error->line, error->column, error->position,
             error->source);
    out_bytes(error->text, strlen(error->text));
    out_end();
}

/*
 *  The allocator: counts, and refuses a block over alloc_cap when it is not 0
 */
static size_t alloc_cap = 0;
static unsigned long alloc_calls = 0;
static unsigned long alloc_refused = 0;
static unsigned long long alloc_bytes = 0;

static inline void *counting_malloc(size_t size) {
    alloc_calls++;
    alloc_bytes += size;
    if (alloc_cap && size > alloc_cap) {
        alloc_refused++;
        return NULL;
    }
    return malloc(size);
}

static inline void counting_free(void *ptr) { free(ptr); }

static inline void use_counting_allocator(size_t cap) {
    alloc_cap = cap;
    json_set_alloc_funcs(counting_malloc, counting_free);
}

/* What the allocator has seen so far, as a line of its own */
static inline void out_allocations(void) {
    out_text("alloc %lu %lu %llu", alloc_calls, alloc_refused, alloc_bytes);
    out_end();
}

/* -v anywhere in the arguments prints every line; the others are returned */
static inline int parse_verbose(int argc, char **argv, char **rest, int max_rest) {
    int i, n = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            out_verbose = 1;
        } else if (n < max_rest) {
            rest[n++] = argv[i];
        }
    }
    return n;
}

#endif
