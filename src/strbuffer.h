/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef STRBUFFER_H
#define STRBUFFER_H

#include "jansson.h"
#include <stdlib.h>

typedef struct {
    char *value;
    size_t length; /* bytes used */
    size_t size;   /* bytes allocated */
} strbuffer_t;

int strbuffer_init(strbuffer_t *strbuff) JANSSON_ATTRS((warn_unused_result));
void strbuffer_close(strbuffer_t *strbuff);

void strbuffer_clear(strbuffer_t *strbuff);

const char *strbuffer_value(const strbuffer_t *strbuff);

/* Steal the value and close the strbuffer */
char *strbuffer_steal_value(strbuffer_t *strbuff);

int strbuffer_append_bytes(strbuffer_t *strbuff, const char *data, size_t size);

/* What strbuffer_append_bytes(strbuff, &byte, 1) does: without the call when
   the byte leaves room for the NUL, which is when that function does not grow
   the buffer; the growth (and its failure) is still that function's. */
static JSON_INLINE int strbuffer_append_byte(strbuffer_t *strbuff, char byte) {
    if (strbuff->size - strbuff->length > 1) {
        strbuff->value[strbuff->length++] = byte;
        strbuff->value[strbuff->length] = '\0';
        return 0;
    }
    return strbuffer_append_bytes(strbuff, &byte, 1);
}

char strbuffer_pop(strbuffer_t *strbuff);

#endif
