/*
 * Copyright (c) 2026, ArtGins.
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

/*
 * Every way out of a dump, for random values (strings of up to 4 099
 * bytes with quotes, backslashes, newlines and slashes; reals; objects
 * with UTF-8 keys; arrays) under five sets of flags:
 *
 * - json_dumps();
 * - json_dump_callback(), the chunks joined (and each chunk checked to be
 *   whole UTF-8 sequences with no NUL, what the callback is promised);
 * - json_dumpb() into buffers of five sizes (its text when it fits);
 * - json_dumpf() and json_dumpfd() into files;
 * - dumps that fail in the middle (an invalid string after some output):
 *   what the callback got before the failure.
 *
 * And the rule of a callback that fails: when it fails, the dump answers
 * -1; when it does not, the dump answers as without the failure, with the
 * same output. Its breaches are counted apart from the digest, on stderr,
 * because jansson 1.31 and before broke it (a failure on the piece of a
 * key was ignored); with --check-callback-rule a breach fails the program.
 *
 *     dumps [-v] [--check-callback-rule]
 */

#include "common.h"

#include <unistd.h>

typedef struct {
    char *text;
    size_t len;
    int fail_at; /* the call that fails, 0 for none */
    int calls;
} received_t;

static long bad_chunks = 0;
static long rule_breaches = 0;

/* Whole UTF-8 sequences, no NUL: what a chunk is promised to be */
static int whole_utf8(const unsigned char *s, size_t n) {
    size_t i = 0, j, k;

    while (i < n) {
        unsigned c = s[i];
        if (c == 0) {
            return 0;
        }
        if (c < 0x80) {
            k = 1;
        } else if ((c >> 5) == 6) {
            k = 2;
        } else if ((c >> 4) == 14) {
            k = 3;
        } else if ((c >> 3) == 30) {
            k = 4;
        } else {
            return 0;
        }
        if (i + k > n) {
            return 0;
        }
        for (j = 1; j < k; j++) {
            if ((s[i + j] & 0xc0) != 0x80) {
                return 0;
            }
        }
        i += k;
    }
    return 1;
}

static int receive(const char *buffer, size_t size, void *data) {
    received_t *r = data;
    char *text;

    r->calls++;
    if (!whole_utf8((const unsigned char *)buffer, size)) {
        bad_chunks++;
    }
    if (r->fail_at && r->calls == r->fail_at) {
        return -1;
    }
    text = realloc(r->text, r->len + size + 1);
    if (!text) {
        fprintf(stderr, "receive(): no memory\n");
        exit(2);
    }
    r->text = text;
    memcpy(r->text + r->len, buffer, size);
    r->len += size;
    return 0;
}

static json_t *gen(int depth) {
    static char s[9000];
    int k = (int)(rnd() % (depth > 4 ? 4 : 7));
    int i, n;

    if (k == 0) {
        return json_integer((int)rnd());
    }
    if (k == 1) {
        return json_real((double)(int)rnd() / 7.0);
    }
    if (k == 2) {
        n = rnd() % 5 == 0 ? 4090 + (int)(rnd() % 10) : (int)(rnd() % 40);
        for (i = 0; i < n; i++) {
            int c = (int)(rnd() % 30);
            s[i] = c == 0   ? '"'
                   : c == 1 ? '\\'
                   : c == 2 ? '\n'
                   : c == 3 ? '/'
                            : (char)('a' + c % 26);
        }
        return json_stringn(s, (size_t)n);
    }
    if (k == 3) {
        return rnd() % 2 ? json_true() : json_null();
    }
    if (k == 4 || k == 5) {
        json_t *object = json_object();
        n = (int)(rnd() % 6);
        for (i = 0; i < n; i++) {
            char key[32];
            snprintf(key, sizeof(key), "k\xc3\xb1%u", rnd() % 100);
            json_object_set_new(object, key, gen(depth + 1));
        }
        return object;
    }
    {
        json_t *array = json_array();
        n = (int)(rnd() % 6);
        for (i = 0; i < n; i++) {
            json_array_append_new(array, gen(depth + 1));
        }
        return array;
    }
}

static void out_file(const char *label, FILE *fp) {
    long size = ftell(fp);
    char *text;

    if (size < 0) {
        fprintf(stderr, "out_file(): ftell() failed\n");
        exit(2);
    }
    text = malloc((size_t)size + 1);
    if (!text) {
        fprintf(stderr, "out_file(): no memory\n");
        exit(2);
    }
    rewind(fp);
    if (fread(text, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "out_file(): fread() failed\n");
        exit(2);
    }
    out_text("%s %ld ", label, size);
    out_bytes(text, (size_t)size);
    out_end();
    free(text);
}

static void all_ways(const json_t *json) {
    static const size_t flags[] = {JSON_COMPACT, 0, JSON_INDENT(3) | JSON_SORT_KEYS,
                                   JSON_ENSURE_ASCII | JSON_ESCAPE_SLASH,
                                   JSON_COMPACT | JSON_ENCODE_ANY};
    size_t f, z;
    int fail_at;

    for (f = 0; f < sizeof(flags) / sizeof(flags[0]); f++) {
        char *text = json_dumps(json, flags[f]);
        size_t len = text ? strlen(text) : 0;
        received_t whole = {NULL, 0, 0, 0};
        int res = json_dump_callback(json, receive, &whole, flags[f]);
        size_t sizes[5];
        FILE *fp;

        out_text("dumps %zx ", flags[f]);
        if (text) {
            out_bytes(text, len);
        } else {
            out_text("NULL");
        }
        out_end();
        out_text("callback %d same %d", res,
                 text && whole.len == len && memcmp(whole.text, text, len) == 0);
        out_end();

        for (fail_at = 1; fail_at <= 3; fail_at++) {
            received_t failing = {NULL, 0, fail_at, 0};
            int res2 = json_dump_callback(json, receive, &failing, flags[f]);
            if (failing.calls >= fail_at ? res2 != -1
                                         : (res2 != res || failing.len != whole.len)) {
                rule_breaches++;
            }
            free(failing.text);
        }

        sizes[0] = 0;
        sizes[1] = 10;
        sizes[2] = len ? len - 1 : 0;
        sizes[3] = len;
        sizes[4] = len + 5;
        for (z = 0; z < 5; z++) {
            char *buf = malloc(sizes[z] + 1);
            size_t needed = json_dumpb(json, sizes[z] ? buf : NULL, sizes[z], flags[f]);
            out_text("dumpb %zu %zu ", sizes[z], needed);
            if (needed && needed <= sizes[z]) {
                out_bytes(buf, needed);
            }
            out_end();
            free(buf);
        }

        fp = tmpfile();
        if (!fp) {
            fprintf(stderr, "all_ways(): tmpfile() failed\n");
            exit(2);
        }
        out_text("dumpf %d", json_dumpf(json, fp, flags[f]));
        out_end();
        out_file("file", fp);
        fclose(fp);

        fp = tmpfile();
        if (!fp) {
            fprintf(stderr, "all_ways(): tmpfile() failed\n");
            exit(2);
        }
        out_text("dumpfd %d", json_dumpfd(json, fileno(fp), flags[f]));
        out_end();
        fflush(fp);
        fseek(fp, 0, SEEK_END);
        out_file("fd", fp);
        fclose(fp);

        free(whole.text);
        free(text);
    }
}

static void failing_in_the_middle(void) {
    char big[5000];
    int n;

    for (n = 0; n < 300; n += 7) {
        json_t *object = json_object();
        received_t r = {NULL, 0, 0, 0};
        int res;

        memset(big, 'x', sizeof(big));
        big[n * 13 % 4999] = '\0';
        json_object_set_new(object, "a", json_string(big));
        json_object_set_new(object, "b", json_stringn_nocheck("ok\xff\xfe", 4));
        json_object_set_new(object, "c", json_integer(n));
        res = json_dump_callback(object, receive, &r, JSON_COMPACT);
        out_text("failing %d %d %zu ", n, res, r.len);
        if (r.text) {
            out_bytes(r.text, r.len);
        }
        out_end();
        free(r.text);
        json_decref(object);
    }
}

int main(int argc, char **argv) {
    char *args[2];
    int nargs = parse_verbose(argc, argv, args, 2);
    int check_rule = 0;
    int it, i;

    for (i = 0; i < nargs; i++) {
        if (strcmp(args[i], "--check-callback-rule") == 0) {
            check_rule = 1;
        }
    }
    rnd_seed(3);
    for (it = 0; it < 3000; it++) {
        json_t *json = gen(0);
        all_ways(json);
        json_decref(json);
    }
    failing_in_the_middle();
    out_finish("dumps");
    fprintf(stderr,
            "dumps: %ld chunks not whole UTF-8, %ld breaches of the callback rule\n",
            bad_chunks, rule_breaches);
    if (bad_chunks) {
        return 1;
    }
    if (check_rule && rule_breaches) {
        return 1;
    }
    return 0;
}
