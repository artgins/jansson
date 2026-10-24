/*
 * Copyright (c) 2026, ArtGins.
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

/*
 * The walks of a value and their loop check: a dump (compact, with sorted
 * keys, json_dumpb()), a deep copy and a recursive update.
 *
 * Chains of 1 to 2 100 containers (objects, arrays, both alternated):
 * without a cycle; with a cycle closed from the last one to depths on both
 * sides of 32 (where the path of a walk moves from its array to its
 * hashtable) and of 2048 (where a dump and a deep copy stop); and with a
 * container shared by two branches, which is not a cycle.
 *
 *     walks [-v]
 */

#include "common.h"

static json_t *chain(int depth, int kind, json_t **nodes) {
    json_t *root = NULL, *prev = NULL;
    int i;

    for (i = 0; i < depth; i++) {
        int is_object = kind == 0 ? 1 : kind == 1 ? 0 : (i % 2 == 0);
        json_t *node = is_object ? json_object() : json_array();

        nodes[i] = node;
        if (is_object) {
            json_object_set_new(node, "v", json_integer(i));
        } else {
            json_array_append_new(node, json_integer(i));
        }
        if (!prev) {
            root = node;
        } else if (json_is_object(prev)) {
            json_object_set_new(prev, "next", node);
        } else {
            json_array_append_new(prev, node);
        }
        prev = node;
    }
    return root;
}

static void link_to(json_t *from, json_t *to) {
    if (json_is_object(from)) {
        json_object_set(from, "back", to);
    } else {
        json_array_append(from, to);
    }
}

static void unlink_from(json_t *from) {
    if (json_is_object(from)) {
        json_object_del(from, "back");
    } else {
        json_array_remove(from, json_array_size(from) - 1);
    }
}

static void walk(json_t *root) {
    json_t *copy;

    out_dump("compact", root, JSON_COMPACT);
    out_dump("sorted", root, JSON_COMPACT | JSON_SORT_KEYS);
    out_text("dumpb %zu", json_dumpb(root, NULL, 0, 0));
    out_end();

    copy = json_deep_copy(root);
    out_text("deep-copy %d", copy != NULL);
    if (copy) {
        out_text(" equal %d", json_equal(copy, root));
        json_decref(copy);
    }
    out_end();

    if (json_is_object(root)) {
        json_t *target = json_object();
        out_text("update %d", json_object_update_recursive(target, root));
        out_end();
        out_dump("updated", target, JSON_COMPACT);
        json_decref(target);
    }
}

int main(int argc, char **argv) {
    static const int depths[] = {1,  2,  3,   31,   32,   33,   34,   40,
                                 64, 65, 100, 2046, 2047, 2048, 2049, 2100};
    static json_t *nodes[3000];
    char *args[1];
    size_t d, t;
    int kind;

    parse_verbose(argc, argv, args, 1);
    for (d = 0; d < sizeof(depths) / sizeof(depths[0]); d++) {
        for (kind = 0; kind < 3; kind++) {
            int depth = depths[d];
            int targets[] = {0, 1, depth / 2, depth - 2, depth - 1, 31, 32, 33};
            json_t *root = chain(depth, kind, nodes);
            json_t *shared, *top;

            out_text("chain %d %d", depth, kind);
            out_end();
            walk(root);

            for (t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
                int k = targets[t];
                if (k < 0 || k >= depth) {
                    continue;
                }
                out_text("cycle to %d", k);
                out_end();
                link_to(nodes[depth - 1], nodes[k]);
                walk(root);
                unlink_from(nodes[depth - 1]);
            }

            shared = json_pack("{s:[i,i]}", "x", 1, 2);
            top = json_pack("{s:O,s:[O]}", "a", shared, "b", shared);
            if (json_is_object(nodes[depth - 1])) {
                json_object_set_new(nodes[depth - 1], "top", top);
            } else {
                json_array_append_new(nodes[depth - 1], top);
            }
            out_text("shared");
            out_end();
            walk(root);
            json_decref(shared);
            json_decref(root);
        }
    }
    out_finish("walks");
    return 0;
}
