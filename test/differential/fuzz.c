/*
 * Copyright (c) 2026, ArtGins.
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

/*
 * The differential fuzz of the lexer and the dump.
 *
 * Random JSON documents (objects, arrays, strings with escapes, \u
 * escapes, surrogate pairs, UTF-8 of two to four bytes, invalid bytes,
 * control bytes and long runs; long numbers, reals, exponents; blanks of
 * every kind), mutated at random (a byte changed, removed or inserted).
 * Each one is loaded by json_loadb() and json_loads() under five sets of
 * flags; a load prints its value dumped under five sets of flags, or its
 * error (code, line, column, position, text). The first 200 bytes of the
 * document, as raw bytes, go through json_stringn() (the UTF-8 check).
 *
 *     fuzz [-v] SEED [ITERATIONS [CAP]]
 *
 * CAP refuses every allocation over CAP bytes (0, the default: none).
 * Every line is printed (the output is the comparison); the lines of the
 * allocator start with "alloc".
 */

#include "common.h"

static char doc[1 << 20];
static size_t doc_len;

static void put(const char *s, size_t n) {
    if (doc_len + n < sizeof(doc)) {
        memcpy(doc + doc_len, s, n);
        doc_len += n;
    }
}

static void put_byte(char c) { put(&c, 1); }

static void put_str(const char *s) { put(s, strlen(s)); }

static void gen_string_body(int max_len) {
    static const char *escapes[] = {
        "\\n", "\\t",     "\\\"",    "\\\\",           "\\/",    "\\b", "\\f",
        "\\r", "\\u00e9", "\\u0000", "\\ud83d\\ude00", "\\uD800"};
    static const char *utf8[] = {"\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
                                 "\xd0\x96"};
    int i, j, n = (int)(rnd() % (uint32_t)max_len);

    for (i = 0; i < n; i++) {
        uint32_t k = rnd() % 100;
        if (k < 70) {
            char c = (char)(0x20 + rnd() % 95);
            if (c == '"' || c == '\\') {
                put_byte('\\');
            }
            put_byte(c);
        } else if (k < 75) {
            put_str(escapes[rnd() % 12]);
        } else if (k < 82) {
            put_str(utf8[rnd() % 4]);
        } else if (k < 85) {
            put_byte((char)(0x80 + rnd() % 128)); /* an invalid lone byte */
        } else if (k < 87) {
            put_byte((char)(rnd() % 0x20)); /* a raw control byte */
        } else if (k < 88) {
            put_byte('\n');
        } else {
            int run = (int)(rnd() % 300);
            for (j = 0; j < run; j++) {
                put_byte((char)('a' + rnd() % 26));
            }
        }
    }
}

static void gen_digits(int n) {
    int i;

    for (i = 0; i < n; i++) {
        put_byte((char)('0' + rnd() % 10));
    }
}

static void gen_value(int depth) {
    static const char *literals[] = {"true", "false", "null"};
    static const char blanks[] = " \t\r\n  ";
    uint32_t k = rnd() % 10;
    char text[64];
    int i, n;

    if (depth > 4) {
        k = 3 + rnd() % 3;
    }
    switch (k) {
        case 0:
        case 1:
            put_byte('{');
            n = (int)(rnd() % 5);
            for (i = 0; i < n; i++) {
                if (i) {
                    put_byte(',');
                }
                put_byte('"');
                gen_string_body(20);
                put_byte('"');
                put_byte(':');
                gen_value(depth + 1);
            }
            put_byte('}');
            break;
        case 2:
            put_byte('[');
            n = (int)(rnd() % 5);
            for (i = 0; i < n; i++) {
                if (i) {
                    put_byte(',');
                }
                gen_value(depth + 1);
            }
            put_byte(']');
            break;
        case 3:
        case 4:
        case 5:
            put_byte('"');
            gen_string_body(k == 5 ? 3000 : 40);
            put_byte('"');
            break;
        case 6:
            if (rnd() % 8 == 0) {
                if (rnd() % 2) {
                    put_byte('-');
                }
                gen_digits(1 + (int)(rnd() % 400));
                if (rnd() % 3 == 0) {
                    put_byte('.');
                    gen_digits((int)(rnd() % 300));
                }
                if (rnd() % 3 == 0) {
                    put_byte('e');
                    if (rnd() % 2) {
                        put_byte('-');
                    }
                    gen_digits((int)(rnd() % 30));
                }
            } else {
                snprintf(text, sizeof(text), "%d", (int)rnd() - 1000000);
                put_str(text);
            }
            break;
        case 7:
            snprintf(text, sizeof(text), "%u.%ue-%u", rnd() % 1000, rnd() % 1000,
                     rnd() % 20);
            put_str(text);
            break;
        case 8:
            put_str(literals[rnd() % 3]);
            break;
        default:
            n = (int)(rnd() % 6);
            for (i = 0; i < n; i++) {
                put_byte(blanks[rnd() % 6]);
            }
            gen_value(depth + 1);
            n = (int)(rnd() % 4);
            for (i = 0; i < n; i++) {
                put_byte(blanks[rnd() % 4]);
            }
            break;
    }
}

static void mutate(void) {
    uint32_t i, m = rnd() % 4;

    for (i = 0; i < m && doc_len > 0; i++) {
        size_t p = rnd() % doc_len;
        uint32_t op = rnd() % 3;
        if (op == 0) {
            doc[p] = (char)rnd();
        } else if (op == 1) {
            memmove(doc + p, doc + p + 1, doc_len - p - 1);
            doc_len--;
        } else if (doc_len + 1 < sizeof(doc)) {
            memmove(doc + p + 1, doc + p, doc_len - p);
            doc[p] = (char)rnd();
            doc_len++;
        }
    }
}

static void dump_all(const json_t *json) {
    static const size_t flags[] = {0, JSON_COMPACT, JSON_ENSURE_ASCII,
                                   JSON_ESCAPE_SLASH | JSON_INDENT(2),
                                   JSON_COMPACT | JSON_ENCODE_ANY};
    size_t i;

    for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        out_dump("dump", json, flags[i] | JSON_ENCODE_ANY);
    }
}

int main(int argc, char **argv) {
    static const size_t load_flags[] = {
        0, JSON_DECODE_ANY | JSON_ALLOW_NUL, JSON_REJECT_DUPLICATES | JSON_DECODE_ANY,
        JSON_DISABLE_EOF_CHECK | JSON_DECODE_ANY, JSON_DECODE_INT_AS_REAL};
    static char text[(1 << 20) + 1];
    char *args[3];
    int nargs = parse_verbose(argc, argv, args, 3);
    int iterations, it, f;

    if (nargs < 1) {
        fprintf(stderr, "usage: fuzz [-v] SEED [ITERATIONS [CAP]]\n");
        return 2;
    }
    out_verbose = 1; /* every line: the output is the comparison */
    rnd_seed(strtoull(args[0], NULL, 10));
    iterations = nargs > 1 ? atoi(args[1]) : 20000;
    use_counting_allocator(nargs > 2 ? (size_t)strtoull(args[2], NULL, 10) : 0);

    for (it = 0; it < iterations; it++) {
        json_t *s;

        doc_len = 0;
        gen_value(0);
        mutate();
        memcpy(text, doc, doc_len);
        text[doc_len] = '\0';

        for (f = 0; f < 10; f++) {
            json_error_t error;
            json_t *json;

            memset(&error, 0, sizeof(error));
            if (f < 5) {
                json = json_loadb(doc, doc_len, load_flags[f], &error);
            } else {
                json = json_loads(text, load_flags[f - 5], &error);
            }
            out_allocations();
            if (json) {
                out_text("load %d %d ok line %d column %d position %d", it, f, error.line,
                         error.column, error.position);
                out_end();
                dump_all(json);
                json_decref(json);
            } else {
                char label[32];
                snprintf(label, sizeof(label), "load %d %d", it, f);
                out_error(label, &error);
            }
        }

        s = json_stringn(doc, doc_len < 200 ? doc_len : 200);
        out_text("utf8 %d %s", it, s ? "valid" : "invalid");
        out_end();
        if (s) {
            dump_all(s);
            json_decref(s);
        }
    }
    return 0;
}
