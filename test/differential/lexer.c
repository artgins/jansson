/*
 * Copyright (c) 2026, ArtGins.
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

/*
 * The edges of the lexer, each one loaded by json_loads() and json_loadb()
 * under five sets of flags: the value dumped, or the error (code, line,
 * column, position, text).
 *
 * - Integers (short, of 14 to 16 digits, too big, "-0", "00") followed by
 *   each of the 256 bytes: in an array, alone, and as the value of a key.
 * - Strings of 0 to 40 bytes with an escape, a \u escape, a quote, a
 *   newline, a control byte, DEL, UTF-8 of two and three bytes, an invalid
 *   byte or a lone lead byte at each place: in an array, and alone.
 * - Keys, strings and numbers of 1 to 300 bytes, across the growths of the
 *   text the lexer saves for a token.
 *
 *     lexer [-v]
 */

#include "common.h"

static void load(const char *doc, size_t len) {
    static const size_t flags[] = {
        0, JSON_DECODE_ANY, JSON_DECODE_ANY | JSON_DECODE_INT_AS_REAL,
        JSON_DECODE_ANY | JSON_ALLOW_NUL, JSON_DECODE_ANY | JSON_DISABLE_EOF_CHECK};
    char *text = malloc(len + 1);
    size_t f;
    int how;

    if (!text) {
        fprintf(stderr, "load(): no memory\n");
        exit(2);
    }
    memcpy(text, doc, len);
    text[len] = '\0';
    for (f = 0; f < sizeof(flags) / sizeof(flags[0]); f++) {
        for (how = 0; how < 2; how++) {
            json_error_t error;
            json_t *json;

            if (how) {
                json = json_loadb(doc, len, flags[f], &error);
            } else {
                json = json_loads(text, flags[f], &error);
            }
            if (json) {
                out_text("ok position %d ", error.position);
                out_end();
                out_dump("value", json, JSON_ENCODE_ANY | JSON_COMPACT);
                json_decref(json);
            } else {
                out_error("load", &error);
            }
        }
    }
    free(text);
}

static void integers(void) {
    static const char *integers[] = {"0",
                                     "7",
                                     "-5",
                                     "12345678901234",
                                     "123456789012345",
                                     "1234567890123456",
                                     "99999999999999999999",
                                     "-0",
                                     "00"};
    char doc[64];
    size_t k;
    int b, n;

    for (k = 0; k < sizeof(integers) / sizeof(integers[0]); k++) {
        for (b = 0; b < 256; b++) {
            n = snprintf(doc, sizeof(doc), "[%s", integers[k]);
            doc[n++] = (char)b;
            doc[n++] = '1';
            doc[n++] = ']';
            load(doc, (size_t)n);

            n = snprintf(doc, sizeof(doc), "%s", integers[k]);
            doc[n++] = (char)b;
            load(doc, (size_t)n);

            n = snprintf(doc, sizeof(doc), "{\"k\":%s", integers[k]);
            doc[n++] = (char)b;
            doc[n++] = '}';
            load(doc, (size_t)n);
        }
    }
}

static void strings(void) {
    static const char *specials[] = {"\"",   "\\",   "\\\"", "\\u00e9",  "\\n",
                                     "\n",   "\x01", "\x7f", "\xc3\xb1", "\xe2\x82\xac",
                                     "\x80", "\xc3", "\xff", "",         "/"};
    char doc[200];
    int len, at, i;
    size_t k;

    for (len = 0; len <= 40; len++) {
        for (at = 0; at <= len; at++) {
            for (k = 0; k < sizeof(specials) / sizeof(specials[0]); k++) {
                size_t n = 0;
                doc[n++] = '[';
                doc[n++] = '"';
                for (i = 0; i < len; i++) {
                    if (i == at) {
                        memcpy(doc + n, specials[k], strlen(specials[k]));
                        n += strlen(specials[k]);
                    }
                    doc[n++] = (char)('a' + i % 26);
                }
                doc[n++] = '"';
                doc[n++] = ',';
                doc[n++] = '2';
                doc[n++] = ']';
                load(doc, n);
                load(doc + 1, n - 4); /* the string alone */
            }
        }
    }
}

static void long_tokens(void) {
    char doc[2000];
    int len, i;

    for (len = 1; len <= 300; len++) {
        size_t n = 0;
        doc[n++] = '{';
        doc[n++] = '"';
        for (i = 0; i < len; i++) {
            doc[n++] = 'k';
        }
        doc[n++] = '"';
        doc[n++] = ':';
        for (i = 0; i < len % 40 + 1; i++) {
            doc[n++] = (char)('1' + i % 9);
        }
        doc[n++] = ',';
        memcpy(doc + n, "\"v\":\"", 5);
        n += 5;
        for (i = 0; i < len; i++) {
            doc[n++] = (char)('a' + i % 26);
        }
        doc[n++] = '"';
        doc[n++] = '}';
        load(doc, n);

        n = 0;
        doc[n++] = '[';
        for (i = 0; i < len; i++) {
            doc[n++] = (char)('1' + i % 9);
        }
        memcpy(doc + n, ".5]", 3);
        n += 3;
        load(doc, n);
    }
}

int main(int argc, char **argv) {
    char *args[1];

    parse_verbose(argc, argv, args, 1);
    integers();
    strings();
    long_tokens();
    out_finish("lexer");
    return 0;
}
