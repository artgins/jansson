/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "jansson_private.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "jansson.h"
#include "strbuffer.h"
#include "utf.h"

#define MAX_INTEGER_STR_LENGTH 25
#define MAX_REAL_STR_LENGTH    25

#define FLAGS_TO_INDENT(f)    ((f) & 0x1F)
#define FLAGS_TO_PRECISION(f) (((f) >> 11) & 0x1F)

struct buffer {
    const size_t size;
    size_t used;
    char *data;
};

static int dump_to_strbuffer(const char *buffer, size_t size, void *data) {
    return strbuffer_append_bytes((strbuffer_t *)data, buffer, size);
}

static int dump_to_buffer(const char *buffer, size_t size, void *data) {
    struct buffer *buf = (struct buffer *)data;

    if (buf->used + size <= buf->size) {
        memcpy(&buf->data[buf->used], buffer, size);
    }

    buf->used += size;
    return 0;
}

static int dump_to_file(const char *buffer, size_t size, void *data) {
    FILE *dest = (FILE *)data;
    if (fwrite(buffer, size, 1, dest) != 1) {
        return -1;
    }
    return 0;
}

static int dump_to_fd(const char *buffer, size_t size, void *data) {
#ifdef HAVE_UNISTD_H
    int *dest = (int *)data;
    if (write(*dest, buffer, size) == (ssize_t)size) {
        return 0;
    }
#endif
    return -1;
}

/* The output of a dump is gathered here and handed to the callback of
   json_dump_callback() when the next piece would not fit, and at the end:
   a call per DUMP_BUFFER_SIZE bytes or so, where there was a call per
   piece (a brace, a quote, a key, a separator, a run of a string). A piece
   is never cut, so what the callback gets is still whole pieces, valid
   UTF-8 as it is promised; a piece bigger than the buffer is handed over
   alone. Once the callback fails, every later piece fails. */
#define DUMP_BUFFER_SIZE 4096

typedef struct {
    json_dump_callback_t callback;
    void *data;
    size_t used;
    int failed;
    char buffer[DUMP_BUFFER_SIZE];
} dump_buffer_t;

static int dump_buffer_flush(dump_buffer_t *out) {
    if (out->failed) {
        return -1;
    }
    if (out->used > 0) {
        if (out->callback(out->buffer, out->used, out->data)) {
            out->failed = 1;
            return -1;
        }
        out->used = 0;
    }
    return 0;
}

/* A piece that does not fit in what is left of the buffer: what the buffer
   holds is handed to the callback first, and a piece bigger than the buffer
   is handed over alone */
static int dump_put_flushing(dump_buffer_t *out, const char *piece, size_t size) {
    if (dump_buffer_flush(out)) {
        return -1;
    }
    if (size > DUMP_BUFFER_SIZE) {
        if (out->callback(piece, size, out->data)) {
            out->failed = 1;
            return -1;
        }
        return 0;
    }
    memcpy(out->buffer, piece, size);
    out->used = size;
    return 0;
}

/* A piece of the dump, copied into the buffer when it fits there */
static JSON_INLINE int dump_put(dump_buffer_t *out, const char *piece, size_t size) {
    if (out->failed) {
        return -1;
    }
    if (size <= DUMP_BUFFER_SIZE - out->used) {
        memcpy(out->buffer + out->used, piece, size);
        out->used += size;
        return 0;
    }
    return dump_put_flushing(out, piece, size);
}

/* 32 spaces (the maximum indentation size) */
static const char whitespace[] = "                                ";

static int dump_indent(size_t flags, int depth, int space, dump_buffer_t *out) {
    if (FLAGS_TO_INDENT(flags) > 0) {
        unsigned int ws_count = FLAGS_TO_INDENT(flags), n_spaces = depth * ws_count;

        if (dump_put(out, "\n", 1)) {
            return -1;
        }

        while (n_spaces > 0) {
            int cur_n =
                n_spaces < sizeof whitespace - 1 ? n_spaces : sizeof whitespace - 1;

            if (dump_put(out, whitespace, cur_n)) {
                return -1;
            }

            n_spaces -= cur_n;
        }
    } else if (space && !(flags & JSON_COMPACT)) {
        return dump_put(out, " ", 1);
    }
    return 0;
}

/* TRUE when none of the eight bytes of w has to be escaped, nor read as
   more than a byte: all of them are plain in a string (jsonp_plain_ascii8())
   and none is '/' with JSON_ESCAPE_SLASH. A word that fails is read a byte
   at a time. */
static JSON_INLINE int plain_ascii8(uint64_t w, size_t flags) {
    if (!jsonp_plain_ascii8(w)) {
        return 0;
    }
    if ((flags & JSON_ESCAPE_SLASH) && HAS_ZERO_BYTE(w ^ BYTES8('/'))) {
        return 0;
    }
    return 1;
}

/* What a byte asks of dump_string(), the bits of byte_class[]: a byte with
   none of the bits in use is written as it is */
#define ESCAPED       1 /* '"', '\\' and the control bytes: always escaped */
#define ESCAPED_SLASH 2 /* '/': escaped with JSON_ESCAPE_SLASH */
#define NOT_ASCII     4 /* a byte of a UTF-8 sequence, read by utf8_iterate() */

// clang-format off
static const unsigned char byte_class[256] = {
    /* 0x00 - 0x1f: the control bytes */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x20 - 0x7f: '"' (0x22), '/' (0x2f) and '\\' (0x5c) */
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x80 - 0xff: the bytes of UTF-8 sequences */
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
};
// clang-format on

/* A string, quoted, its runs of plain bytes written whole and each
   codepoint to escape written escaped. Fails (-1) on a piece that cannot be
   written and on invalid UTF-8, where it stops: what was written before
   stays written. */
static int dump_string(const char *str, size_t len, dump_buffer_t *out, size_t flags) {
    const unsigned char mask =
        ESCAPED | NOT_ASCII | ((flags & JSON_ESCAPE_SLASH) ? ESCAPED_SLASH : 0);
    const char *pos, *end, *lim;
    int32_t codepoint = 0;

    if (dump_put(out, "\"", 1)) {
        return -1;
    }

    pos = str;
    lim = str + len;
    while (1) {
        const char *text;
        char seq[13];
        int length;

        /* The run from str up to pos: up to the end of the string, or up to
           a codepoint to escape, which ends at end */
        while (pos < lim) {
            const char *stop;
            uint64_t w;

            /* what the byte by byte loop does for eight such bytes */
            while (lim - pos >= 8) {
                memcpy(&w, pos, 8);
                if (!plain_ascii8(w, flags)) {
                    break;
                }
                pos += 8;
            }
            if (pos == lim) {
                break;
            }

            /* A byte at a time up to where a word can be read again: through
               the bytes of the word that failed, or the last bytes */
            stop = lim - pos >= 8 ? pos + 8 : lim;
            do {
                unsigned char c = (unsigned char)*pos;

                if (!(byte_class[c] & mask)) {
                    pos++;
                    continue;
                }
                if (c < 0x80) {
                    /* '"', '\\', a control byte, or '/' with JSON_ESCAPE_SLASH */
                    codepoint = c;
                    end = pos + 1;
                    goto escape;
                }
                end = utf8_iterate(pos, lim - pos, &codepoint);
                if (!end) {
                    return -1;
                }
                if (flags & JSON_ENSURE_ASCII) {
                    goto escape;
                }
                pos = end;
            } while (pos < stop);
        }

        /* The end of the string */
        if (pos != str) {
            if (dump_put(out, str, pos - str)) {
                return -1;
            }
        }
        return dump_put(out, "\"", 1);

    escape:
        if (pos != str) {
            if (dump_put(out, str, pos - str)) {
                return -1;
            }
        }

        /* handle \, /, ", and control codes */
        length = 2;
        switch (codepoint) {
            case '\\':
                text = "\\\\";
                break;
            case '\"':
                text = "\\\"";
                break;
            case '\b':
                text = "\\b";
                break;
            case '\f':
                text = "\\f";
                break;
            case '\n':
                text = "\\n";
                break;
            case '\r':
                text = "\\r";
                break;
            case '\t':
                text = "\\t";
                break;
            case '/':
                text = "\\/";
                break;
            default: {
                /* codepoint is in BMP */
                if (codepoint < 0x10000) {
                    snprintf(seq, sizeof(seq), "\\u%04X", (unsigned int)codepoint);
                    length = 6;
                }

                /* not in BMP -> construct a UTF-16 surrogate pair */
                else {
                    int32_t first, last;

                    codepoint -= 0x10000;
                    first = 0xD800 | ((codepoint & 0xffc00) >> 10);
                    last = 0xDC00 | (codepoint & 0x003ff);

                    snprintf(seq, sizeof(seq), "\\u%04X\\u%04X", (unsigned int)first,
                             (unsigned int)last);
                    length = 12;
                }

                text = seq;
                break;
            }
        }

        if (dump_put(out, text, length)) {
            return -1;
        }

        str = pos = end;
    }
}

struct key_len {
    const char *key;
    int len;
};

static int compare_keys(const void *key1, const void *key2) {
    const struct key_len *k1 = key1;
    const struct key_len *k2 = key2;
    const size_t min_size = k1->len < k2->len ? k1->len : k2->len;
    int res = memcmp(k1->key, k2->key, min_size);

    if (res) {
        return res;
    }

    return k1->len - k2->len;
}

static int do_dump(const json_t *json, size_t flags, int depth, loop_path_t *parents,
                   dump_buffer_t *out) {
    int embed = flags & JSON_EMBED;

    flags &= ~JSON_EMBED;

    if (!json) {
        return -1;
    }

    if (depth >= JSON_PARSER_MAX_DEPTH) {
        return -1;
    }

    switch (json_typeof(json)) {
        case JSON_NULL:
            return dump_put(out, "null", 4);

        case JSON_TRUE:
            return dump_put(out, "true", 4);

        case JSON_FALSE:
            return dump_put(out, "false", 5);

        case JSON_INTEGER: {
            char buffer[MAX_INTEGER_STR_LENGTH];
            int size;

            size = snprintf(buffer, MAX_INTEGER_STR_LENGTH, "%" JSON_INTEGER_FORMAT,
                            json_integer_value(json));
            if (size < 0 || size >= MAX_INTEGER_STR_LENGTH) {
                return -1;
            }

            return dump_put(out, buffer, size);
        }

        case JSON_REAL: {
            char buffer[MAX_REAL_STR_LENGTH];
            int size;
            double value = json_real_value(json);

            size = jsonp_dtostr(buffer, MAX_REAL_STR_LENGTH, value,
                                FLAGS_TO_PRECISION(flags));
            if (size < 0) {
                return -1;
            }

            return dump_put(out, buffer, size);
        }

        case JSON_STRING:
            return dump_string(json_string_value(json), json_string_length(json), out,
                               flags);

        case JSON_ARRAY: {
            size_t n;
            size_t i;

            /* detect circular references */
            if (jsonp_loop_check(parents, json)) {
                return -1;
            }

            n = json_array_size(json);

            if (!embed && dump_put(out, "[", 1)) {
                return -1;
            }
            if (n == 0) {
                jsonp_loop_leave(parents, json);
                return embed ? 0 : dump_put(out, "]", 1);
            }
            if (dump_indent(flags, depth + 1, 0, out)) {
                return -1;
            }

            for (i = 0; i < n - 1; ++i) {
                if (do_dump(json_array_get(json, i), flags, depth + 1, parents, out)) {
                    return -1;
                }

                if (dump_put(out, ",", 1) || dump_indent(flags, depth + 1, 1, out)) {
                    return -1;
                }
            }

            if (do_dump(json_array_get(json, i), flags, depth + 1, parents, out)) {
                return -1;
            }
            if (dump_indent(flags, depth, 0, out)) {
                return -1;
            }

            jsonp_loop_leave(parents, json);
            return embed ? 0 : dump_put(out, "]", 1);
        }

        case JSON_OBJECT: {
            void *iter;
            const char *separator;
            int separator_length;

            if (flags & JSON_COMPACT) {
                separator = ":";
                separator_length = 1;
            } else {
                separator = ": ";
                separator_length = 2;
            }

            /* detect circular references */
            if (jsonp_loop_check(parents, json)) {
                return -1;
            }

            if (!embed && dump_put(out, "{", 1)) {
                return -1;
            }

            iter = json_object_iter((json_t *)json);
            if (!iter) {
                jsonp_loop_leave(parents, json);
                return embed ? 0 : dump_put(out, "}", 1);
            }
            if (dump_indent(flags, depth + 1, 0, out)) {
                return -1;
            }

            if (flags & JSON_SORT_KEYS) {
                struct key_len *keys;
                size_t size, i;

                size = json_object_size(json);
                keys = jsonp_malloc(size * sizeof(struct key_len));
                if (!keys) {
                    return -1;
                }

                i = 0;
                while (iter) {
                    struct key_len *keylen = &keys[i];

                    keylen->key = json_object_iter_key(iter);
                    keylen->len = json_object_iter_key_len(iter);

                    iter = json_object_iter_next((json_t *)json, iter);
                    i++;
                }
                assert(i == size);

                qsort(keys, size, sizeof(struct key_len), compare_keys);

                for (i = 0; i < size; i++) {
                    const struct key_len *key;
                    json_t *value;

                    key = &keys[i];
                    value = json_object_getn(json, key->key, key->len);
                    assert(value);

                    dump_string(key->key, key->len, out, flags);
                    if (dump_put(out, separator, separator_length) ||
                        do_dump(value, flags, depth + 1, parents, out)) {
                        jsonp_free(keys);
                        return -1;
                    }

                    if (i < size - 1) {
                        if (dump_put(out, ",", 1) ||
                            dump_indent(flags, depth + 1, 1, out)) {
                            jsonp_free(keys);
                            return -1;
                        }
                    } else {
                        if (dump_indent(flags, depth, 0, out)) {
                            jsonp_free(keys);
                            return -1;
                        }
                    }
                }

                jsonp_free(keys);
            } else {
                /* Don't sort keys */

                while (iter) {
                    void *next = json_object_iter_next((json_t *)json, iter);
                    const char *key = json_object_iter_key(iter);
                    const size_t key_len = json_object_iter_key_len(iter);

                    dump_string(key, key_len, out, flags);
                    if (dump_put(out, separator, separator_length) ||
                        do_dump(json_object_iter_value(iter), flags, depth + 1, parents,
                                out)) {
                        return -1;
                    }

                    if (next) {
                        if (dump_put(out, ",", 1) ||
                            dump_indent(flags, depth + 1, 1, out)) {
                            return -1;
                        }
                    } else {
                        if (dump_indent(flags, depth, 0, out)) {
                            return -1;
                        }
                    }

                    iter = next;
                }
            }

            jsonp_loop_leave(parents, json);
            return embed ? 0 : dump_put(out, "}", 1);
        }

        default:
            /* not reached */
            return -1;
    }
}

char *json_dumps(const json_t *json, size_t flags) {
    strbuffer_t strbuff;
    char *result;

    if (strbuffer_init(&strbuff)) {
        return NULL;
    }

    if (json_dump_callback(json, dump_to_strbuffer, (void *)&strbuff, flags)) {
        result = NULL;
    } else {
        char *new_result;
        result = strbuffer_steal_value(&strbuff);
        // technically the resizing is not needed.
        new_result = jsonp_realloc(result, strbuff.size, strbuff.length + 1);
        if (new_result) { // when realloc fails we just use the original pointer
            result = new_result;
        }
    }

    strbuffer_close(&strbuff);
    return result;
}

size_t json_dumpb(const json_t *json, char *buffer, size_t size, size_t flags) {
    struct buffer buf = {size, 0, buffer};

    if (json_dump_callback(json, dump_to_buffer, (void *)&buf, flags)) {
        return 0;
    }

    return buf.used;
}

int json_dumpf(const json_t *json, FILE *output, size_t flags) {
    return json_dump_callback(json, dump_to_file, (void *)output, flags);
}

int json_dumpfd(const json_t *json, int output, size_t flags) {
    return json_dump_callback(json, dump_to_fd, (void *)&output, flags);
}

int json_dump_file(const json_t *json, const char *path, size_t flags) {
    int result;

    FILE *output = fopen(path, "w");
    if (!output) {
        return -1;
    }

    result = json_dumpf(json, output, flags);

    if (fclose(output) != 0) {
        return -1;
    }

    return result;
}

int json_dump_callback(const json_t *json, json_dump_callback_t callback, void *data,
                       size_t flags) {
    int res;
    loop_path_t parents_set;
    dump_buffer_t out;

    if (!(flags & JSON_ENCODE_ANY)) {
        if (!json_is_array(json) && !json_is_object(json)) {
            return -1;
        }
    }

    out.callback = callback;
    out.data = data;
    out.used = 0;
    out.failed = 0;

    jsonp_loop_init(&parents_set);
    res = do_dump(json, flags, 0, &parents_set, &out);
    jsonp_loop_close(&parents_set);

    /* What the dump wrote is handed over even when it failed afterwards, as
       each piece was when it was written */
    if (dump_buffer_flush(&out)) {
        res = -1;
    }

    return res;
}
