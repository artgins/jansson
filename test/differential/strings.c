/*
 * Copyright (c) 2026, ArtGins.
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

/*
 * Strings: their UTF-8 check, their value, and their dump.
 *
 * - One special byte at each place of strings of 0 to 100 bytes, and two
 *   at every pair of places of strings of 0 to 34 bytes (an escape, a
 *   control byte, '/', DEL, UTF-8 of two to four bytes, invalid bytes):
 *   json_stringn() of each (the UTF-8 check) and its dump under the flags
 *   that change how a string is written. The words of eight bytes that the
 *   check and the dump read at once have their edges in there.
 * - Values of 0 to 2000 bytes, with and without a NUL inside: made, set to
 *   other lengths and back, copied, deep copied, compared and dumped
 *   (both sides of the size up to which a value shares the block of its
 *   string).
 * - Random strings of ASCII and UTF-8 pieces, valid and not, at any
 *   alignment: their UTF-8 check.
 *
 *     strings [-v]
 */

#include "common.h"

static const char *specials[] = {"\"",
                                 "\\",
                                 "/",
                                 "\n",
                                 "\x01",
                                 "\x1f",
                                 "\x7f",
                                 "\xc3\xb1",
                                 "\xe2\x82\xac",
                                 "\xf0\x9f\x98\x80",
                                 "\x80",
                                 "\xc3",
                                 " ",
                                 "~"};
#define N_SPECIALS (sizeof(specials) / sizeof(specials[0]))

static const size_t dump_flags[] = {0,
                                    JSON_ESCAPE_SLASH,
                                    JSON_ENSURE_ASCII,
                                    JSON_ESCAPE_SLASH | JSON_ENSURE_ASCII,
                                    JSON_COMPACT,
                                    JSON_ENCODE_ANY};
#define N_DUMP_FLAGS (sizeof(dump_flags) / sizeof(dump_flags[0]))

static void check_and_dump(const char *s, size_t len, size_t n_flags) {
    json_t *string = json_stringn(s, len);
    json_t *array;
    size_t f;

    out_text("check %zu %s", len, string ? "valid" : "invalid");
    out_end();
    if (!string) {
        return;
    }
    array = json_pack("[o]", string);
    for (f = 0; f < n_flags; f++) {
        out_dump("dump", array, dump_flags[f]);
        out_dump("dump-any", string, dump_flags[f] | JSON_ENCODE_ANY);
    }
    json_decref(array);
}

static void one_special_at_each_place(void) {
    char s[200];
    int len, at, i;
    size_t k;

    for (len = 0; len <= 100; len++) {
        for (at = 0; at <= len; at++) {
            for (k = 0; k < N_SPECIALS; k++) {
                size_t n = 0;
                for (i = 0; i < at; i++) {
                    s[n++] = (char)('a' + i % 26);
                }
                memcpy(s + n, specials[k], strlen(specials[k]));
                n += strlen(specials[k]);
                for (i = at; i < len; i++) {
                    s[n++] = (char)('A' + i % 26);
                }
                check_and_dump(s, n, N_DUMP_FLAGS);
            }
        }
    }
}

static void two_specials_at_every_pair(void) {
    char s[200];
    int len, a, b, i;
    size_t ka, kb;

    for (len = 0; len <= 34; len++) {
        for (a = 0; a <= len; a++) {
            for (b = a; b <= len; b++) {
                for (ka = 0; ka < N_SPECIALS - 2; ka++) {
                    for (kb = 0; kb < N_SPECIALS - 2; kb++) {
                        size_t n = 0;
                        for (i = 0; i <= len; i++) {
                            if (i == a) {
                                memcpy(s + n, specials[ka], strlen(specials[ka]));
                                n += strlen(specials[ka]);
                            }
                            if (i == b) {
                                memcpy(s + n, specials[kb], strlen(specials[kb]));
                                n += strlen(specials[kb]);
                            }
                            if (i < len) {
                                s[n++] = (char)('a' + i % 26);
                            }
                        }
                        check_and_dump(s, n, 4);
                    }
                }
            }
        }
    }
}

static void out_string(const char *label, const json_t *string) {
    out_text("%s %zu ", label, json_string_length(string));
    out_bytes(json_string_value(string), json_string_length(string) + 1);
    out_end();
}

static void values(void) {
    char *buf = malloc(3000);
    int len, i;

    if (!buf) {
        fprintf(stderr, "values(): no memory\n");
        exit(2);
    }
    for (i = 0; i < 3000; i++) {
        buf[i] = (char)('a' + i % 26);
    }
    for (len = 0; len <= 2000; len++) {
        char saved = buf[len / 2];
        int len2 = (len * 37) % 2001;
        json_t *a, *b, *c, *d, *array;

        if (len % 7 == 0 && len) {
            buf[len / 2] = '\0'; /* a NUL inside */
        }
        a = json_stringn(buf, (size_t)len);
        b = json_stringn_nocheck(buf, (size_t)len);
        if (!a || !b) {
            out_text("make %d failed", len);
            out_end();
            json_decref(a);
            json_decref(b);
            buf[len / 2] = saved;
            continue;
        }
        out_string("a", a);
        out_string("b", b);
        c = json_deep_copy(a);
        d = json_copy(b);
        out_string("c", c);
        out_string("d", d);
        out_text("equal %d %d %d", json_equal(a, b), json_equal(a, c), json_equal(b, d));
        out_end();

        out_text("set %d", json_string_setn_nocheck(a, buf + 1, (size_t)len2));
        out_end();
        out_string("a", a);
        out_text("set %d", json_string_setn(b, buf + 2, (size_t)len2 / 2));
        out_end();
        out_string("b", b);
        out_text("set %d", json_string_setn_nocheck(a, buf, (size_t)len));
        out_end();
        out_string("a", a);
        out_text("equal %d", json_equal(a, c));
        out_end();

        array = json_pack("[OOOO]", a, b, c, d);
        out_dump("dump", array, JSON_ENCODE_ANY);
        json_decref(array);
        json_decref(a);
        json_decref(b);
        json_decref(c);
        json_decref(d);
        buf[len / 2] = saved;
    }
    {
        json_t *s = json_string("x");
        out_text("set %d", json_string_set(s, "a much longer value than the first one"));
        out_end();
        out_string("s", s);
        out_text("set %d", json_string_set(s, ""));
        out_end();
        out_string("s", s);
        json_decref(s);
        s = json_sprintf("%s-%d", "abc", 12345);
        out_string("sprintf", s);
        json_decref(s);
    }
    free(buf);
}

static void utf8_checks(void) {
    static const unsigned char pieces[][5] = {{'a'},
                                              {'Z'},
                                              {' '},
                                              {0x7f},
                                              {0},
                                              {0xc3, 0xb1},
                                              {0xe2, 0x82, 0xac},
                                              {0xf0, 0x9f, 0x98, 0x80},
                                              {0x80},
                                              {0xc3},
                                              {0xe2, 0x82},
                                              {0xf4, 0x90, 0x80, 0x80},
                                              {0xc0, 0x80},
                                              {0xed, 0xa0, 0x80},
                                              {0xff}};
    static const int piece_len[] = {1, 1, 1, 1, 1, 2, 3, 4, 1, 1, 2, 4, 2, 3, 1};
    char s[200], buf[220];
    long k;
    int m, p, q;

    for (k = 0; k < 2000000; k++) {
        int parts = (int)(rnd() % 12);
        int off = (int)(rnd() % 4); /* any alignment */
        json_t *string;

        m = 0;
        for (p = 0; p < parts; p++) {
            if (rnd() % 100 < 60) {
                int run = (int)(rnd() % 20);
                for (q = 0; q < run && m < 190; q++) {
                    s[m++] = (char)('a' + rnd() % 26);
                }
            } else {
                int pi = (int)(rnd() % 15);
                if (m + piece_len[pi] < 190) {
                    memcpy(s + m, pieces[pi], (size_t)piece_len[pi]);
                    m += piece_len[pi];
                }
            }
        }
        memcpy(buf + off, s, (size_t)m);
        string = json_stringn(buf + off, (size_t)m);
        out_text("utf8 %d", string != NULL);
        out_end();
        json_decref(string);
    }
}

int main(int argc, char **argv) {
    char *args[1];

    parse_verbose(argc, argv, args, 1);
    rnd_seed(88172645463325252ULL);
    one_special_at_each_place();
    two_specials_at_every_pair();
    values();
    utf8_checks();
    out_finish("strings");
    return 0;
}
