/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef JANSSON_PRIVATE_H
#define JANSSON_PRIVATE_H

#include "hashtable.h"
#include "jansson.h"
#ifdef HAVE_CONFIG_H
#include "jansson_private_config.h"
#endif
#include "strbuffer.h"
#include <stddef.h>
#include <stdint.h>

#define container_of(ptr_, type_, member_)                                               \
    ((type_ *)((char *)ptr_ - offsetof(type_, member_)))

/* On some platforms, max() may already be defined */
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/* va_copy is a C99 feature. In C89 implementations, it's sometimes
   available as __va_copy. If not, memcpy() should do the trick. */
#ifndef va_copy
#ifdef __va_copy
#define va_copy __va_copy
#else
#define va_copy(a, b) memcpy(&(a), &(b), sizeof(va_list))
#endif
#endif

typedef struct {
    json_t json;
    hashtable_t hashtable;
} json_object_t;

typedef struct {
    json_t json;
    size_t size;
    size_t entries;
    json_t **table;
} json_array_t;

typedef struct {
    json_t json;
    char *value;
    size_t length;
} json_string_t;

typedef struct {
    json_t json;
    double value;
} json_real_t;

typedef struct {
    json_t json;
    json_int_t value;
} json_integer_t;

#define json_to_object(json_)  container_of(json_, json_object_t, json)
#define json_to_array(json_)   container_of(json_, json_array_t, json)
#define json_to_string(json_)  container_of(json_, json_string_t, json)
#define json_to_real(json_)    container_of(json_, json_real_t, json)
#define json_to_integer(json_) container_of(json_, json_integer_t, json)

/* Create a string by taking ownership of an existing buffer */
json_t *jsonp_stringn_nocheck_own(const char *value, size_t len);

/* Error message formatting */
void jsonp_error_init(json_error_t *error, const char *source);
void jsonp_error_set_source(json_error_t *error, const char *source);
void jsonp_error_set(json_error_t *error, int line, int column, size_t position,
                     enum json_error_code code, const char *msg, ...);
void jsonp_error_vset(json_error_t *error, int line, int column, size_t position,
                      enum json_error_code code, const char *msg, va_list ap);

/* Locale independent string<->double conversions */
int jsonp_strtod(strbuffer_t *strbuffer, double *out);
int jsonp_dtostr(char *buffer, size_t size, double value, int prec);

/* Wrappers for custom memory functions */
void *jsonp_malloc(size_t size) JANSSON_ATTRS((warn_unused_result));
void *jsonp_realloc(void *ptr, size_t originalSize, size_t newSize)
    JANSSON_ATTRS((warn_unused_result));
void jsonp_free(void *ptr);
char *jsonp_strndup(const char *str, size_t len) JANSSON_ATTRS((warn_unused_result));

/* Circular reference check*/
/* Space for "0x", double the sizeof a pointer for the hex and a terminator. */
#define LOOP_KEY_LEN (2 + (sizeof(json_t *) * 2) + 1)

/* The containers on the way from the root of a walk (a dump, a deep copy, a
   recursive update) down to where it is: a container met again among them
   is a cycle. The first LOOP_PATH_SHALLOW of them are kept in an array and
   compared one by one; the deeper ones in a hashtable by their address,
   made the first time a walk goes that deep. */
#define LOOP_PATH_SHALLOW 32
typedef struct {
    const json_t *shallow[LOOP_PATH_SHALLOW];
    size_t depth;
    int deep_init;
    hashtable_t deep;
} loop_path_t;

void jsonp_loop_init(loop_path_t *path);
void jsonp_loop_close(loop_path_t *path);
/* -1 when json is on the path (a cycle) or no memory; else json is added */
int jsonp_loop_check(loop_path_t *path, const json_t *json);
/* Take json, the last one added, off the path */
void jsonp_loop_leave(loop_path_t *path, const json_t *json);

/* Eight bytes of a string at once, in a word read with memcpy(). Each test
   is exact (a byte that meets it is never missed) and does not depend on
   the byte order. */
#define BYTES8(b)        (UINT64_C(0x0101010101010101) * (b))
#define HAS_ZERO_BYTE(v) (((v) - BYTES8(0x01)) & ~(v) & BYTES8(0x80))

/* TRUE when none of the eight bytes of w is special in a JSON string: all
   of them are ASCII from 0x20 up, and none is '"' nor '\\' */
static JSON_INLINE int jsonp_plain_ascii8(uint64_t w) {
    if (w & BYTES8(0x80))
        return 0;
    if ((w - BYTES8(0x20)) & ~w & BYTES8(0x80)) /* a byte under 0x20 */
        return 0;
    if (HAS_ZERO_BYTE(w ^ BYTES8('"')) || HAS_ZERO_BYTE(w ^ BYTES8('\\')))
        return 0;
    return 1;
}

/* Windows compatibility */
#if defined(_WIN32) || defined(WIN32)
#if defined(_MSC_VER) /* MS compiller */
#if (_MSC_VER < 1900) &&                                                                 \
    !defined(snprintf) /* snprintf not defined yet & not introduced */
#define snprintf _snprintf
#endif
#if (_MSC_VER < 1500) &&                                                                 \
    !defined(vsnprintf) /* vsnprintf not defined yet & not introduced */
#define vsnprintf(b, c, f, a) _vsnprintf(b, c, f, a)
#endif
#else /* Other Windows compiller, old definition */
#define snprintf  _snprintf
#define vsnprintf _vsnprintf
#endif
#endif

#endif
