/*
 * Copyright (c) 2026, ArtGins.
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

/*
 * Objects: their hashtable, and the keys of the parser.
 *
 * - Objects of 200 000 keys of three shapes (numbers, words with numbers,
 *   treedb-like references): inserted, replaced, looked up, deleted,
 *   walked in their order, and dumped with their keys sorted.
 * - Nested objects with keys of up to 3 000 bytes, with escapes, \u
 *   escapes, surrogate pairs, UTF-8 and repeated keys, loaded by
 *   json_loads() and by json_loadb() with JSON_REJECT_DUPLICATES: the
 *   value dumped, or the error.
 *
 *     objects [-v]
 */

#include "common.h"

static void key_of(char *key, size_t size, int shape, int i) {
    if (shape == 0) {
        snprintf(key, size, "%d", i);
    } else if (shape == 1) {
        snprintf(key, size, "key_%d_x", i * 7919);
    } else {
        snprintf(key, size, "departments^%08x-%04x^users", (unsigned)(i * 2654435761u),
                 i & 0xffff);
    }
}

static void big_object(int shape) {
    const int n = 200000;
    json_t *object = json_object();
    const char *key;
    json_t *value;
    char k[96];
    int i;

    for (i = 0; i < n; i++) {
        key_of(k, sizeof(k), shape, i);
        json_object_set_new(object, k, json_integer(i));
        if (i % 3 == 0) {
            json_object_set_new(object, k, json_integer(-i)); /* replaced */
        }
    }
    for (i = 0; i < n; i += 2) {
        key_of(k, sizeof(k), shape, i);
        value = json_object_get(object, k);
        out_text("get %s %lld", k, value ? (long long)json_integer_value(value) : -1LL);
        out_end();
        if (i % 4 == 0) {
            out_text("del %s %d", k, json_object_del(object, k));
            out_end();
        }
        out_text("get %s %d", k, json_object_get(object, k) != NULL);
        out_end();
    }
    json_object_foreach(object, key, value) {
        out_text("walk %s %lld", key, (long long)json_integer_value(value));
        out_end();
    }
    out_dump("sorted", object, JSON_COMPACT | JSON_SORT_KEYS);
    out_text("size %zu", json_object_size(object));
    out_end();
    json_decref(object);
}

static char doc[1 << 22];
static size_t doc_len;

static void put(const char *s, size_t n) {
    if (doc_len + n < sizeof(doc)) {
        memcpy(doc + doc_len, s, n);
        doc_len += n;
    }
}

static void gen_key(void) {
    int n = rnd() % 4 == 0 ? 200 + (int)(rnd() % 3000) : (int)(rnd() % 20);
    int i;

    put("\"", 1);
    for (i = 0; i < n; i++) {
        int k = (int)(rnd() % 40);
        if (k == 0) {
            put("\\n", 2);
        } else if (k == 1) {
            put("\\u00e9", 6);
        } else if (k == 2) {
            put("\\ud83d\\ude00", 12);
        } else if (k == 3) {
            put("\\\"", 2);
        } else if (k == 4) {
            put("\xc3\xb1", 2);
        } else {
            char c = (char)('a' + rnd() % 26);
            put(&c, 1);
        }
    }
    put("\"", 1);
}

static void gen_value(int depth) {
    int k = (int)(rnd() % (depth > 6 ? 3 : 5));
    int i, n;

    if (k == 0) {
        char t[32];
        int l = snprintf(t, sizeof(t), "%d", (int)rnd());
        put(t, (size_t)l);
    } else if (k == 1) {
        gen_key();
    } else if (k == 2) {
        put("null", 4);
    } else if (k == 3) {
        put("[", 1);
        n = (int)(rnd() % 4);
        for (i = 0; i < n; i++) {
            if (i) {
                put(",", 1);
            }
            gen_value(depth + 1);
        }
        put("]", 1);
    } else {
        put("{", 1);
        n = (int)(rnd() % 5);
        for (i = 0; i < n; i++) {
            if (i) {
                put(",", 1);
            }
            if (rnd() % 8 == 0 && i) {
                put("\"dup\"", 5); /* a repeated key */
            } else {
                gen_key();
            }
            put(":", 1);
            gen_value(depth + 1);
        }
        put("}", 1);
    }
}

static void nested_keys(void) {
    int it, i, n, how;

    for (it = 0; it < 4000; it++) {
        doc_len = 0;
        put("{", 1);
        n = 1 + (int)(rnd() % 6);
        for (i = 0; i < n; i++) {
            if (i) {
                put(",", 1);
            }
            gen_key();
            put(":", 1);
            gen_value(0);
        }
        put("}", 1);
        doc[doc_len] = '\0';

        for (how = 0; how < 2; how++) {
            json_error_t error;
            json_t *json;

            if (how) {
                json = json_loadb(doc, doc_len, JSON_REJECT_DUPLICATES, &error);
            } else {
                json = json_loads(doc, 0, &error);
            }
            if (json) {
                out_dump("value", json, JSON_ENCODE_ANY);
                json_decref(json);
            } else {
                out_error("load", &error);
            }
        }
    }
}

int main(int argc, char **argv) {
    char *args[1];
    int shape;

    parse_verbose(argc, argv, args, 1);
    for (shape = 0; shape < 3; shape++) {
        big_object(shape);
    }
    rnd_seed(7);
    nested_keys();
    out_finish("objects");
    return 0;
}
