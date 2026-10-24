/*
 * Copyright (c) 2026, ArtGins.
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

/*
 * Arrays built by random appends, inserts at any place, removes, sets,
 * clears and extends (by another array and by themselves), across the
 * growths of their table; then copied, deep copied, compared and dumped.
 *
 *     arrays [-v]
 */

#include "common.h"

int main(int argc, char **argv) {
    char *args[1];
    int it, op;

    parse_verbose(argc, argv, args, 1);
    rnd_seed(9);
    for (it = 0; it < 4000; it++) {
        json_t *array = json_array();
        json_t *copy, *deep;
        int ops = (int)(rnd() % 60);

        for (op = 0; op < ops; op++) {
            int k = (int)(rnd() % 10);
            size_t n = json_array_size(array);
            int res;

            if (k < 4) {
                res = json_array_append_new(array, json_integer(op));
            } else if (k == 4) {
                res = json_array_insert_new(array, n ? rnd() % (n + 1) : 0,
                                            json_integer(-op));
            } else if (k == 5 && n) {
                res = json_array_remove(array, rnd() % n);
            } else if (k == 6) {
                json_t *other = json_pack("[iii]", 1, 2, 3);
                res = json_array_extend(array, other);
                json_decref(other);
            } else if (k == 7 && n < 40) {
                res = json_array_extend(array, array);
            } else if (k == 8 && rnd() % 8 == 0) {
                res = json_array_clear(array);
            } else if (n) {
                res = json_array_set_new(array, rnd() % n, json_string("s"));
            } else {
                res = 9;
            }
            out_text("op %d %d %d %zu", k, op, res, json_array_size(array));
            out_end();
        }
        copy = json_copy(array);
        deep = json_deep_copy(array);
        out_text("equal %d %d", json_equal(array, copy), json_equal(array, deep));
        out_end();
        out_dump("array", array, JSON_COMPACT);
        out_dump("deep", deep, 0);
        json_decref(array);
        json_decref(copy);
        json_decref(deep);
    }
    out_finish("arrays");
    return 0;
}
