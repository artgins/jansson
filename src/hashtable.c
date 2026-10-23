/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifdef HAVE_CONFIG_H
#include <jansson_private_config.h>
#endif

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include "hashtable.h"
#include "jansson_private.h" /* for container_of() */
#include <jansson_config.h>  /* for JSON_INLINE */

#ifndef INITIAL_HASHTABLE_ORDER
#define INITIAL_HASHTABLE_ORDER 3
#endif

typedef struct hashtable_list list_t;
typedef struct hashtable_pair pair_t;
typedef struct hashtable_bucket bucket_t;

extern volatile uint32_t hashtable_seed;

#define list_to_pair(list_)         container_of(list_, pair_t, list)
#define ordered_list_to_pair(list_) container_of(list_, pair_t, ordered_list)

/* The size and the mask of a table of 2^n buckets (were in lookup3.h) */
#define hashsize(n) ((size_t)1 << (n))
#define hashmask(n) (hashsize(n) - 1)

/*
 * The hash of a key.
 *
 * Words of 64 bits of the key, xored with the seed and with constants,
 * multiplied 64x64 -> 128 and folded to 64 bits: the construction of
 * wyhash (Wang Yi, public domain), the one Go's runtime uses for its maps
 * where it has no AES. It replaces lookup3's hashlittle(), which hashed a
 * key 32 bits at a time and was the most expensive function of a lookup.
 *
 * Nothing that jansson answers depends on the hash: an object is walked in
 * the order its keys were inserted (ordered_list), never by its buckets.
 * The hash is seeded with hashtable_seed, as before (json_object_seed()),
 * so the buckets of a key are not known outside the process.
 */
#define HASH_P0 0xa0761d6478bd642fULL
#define HASH_P1 0xe7037ed1a0b428dbULL
#define HASH_P2 0x8ebc6af09c88c6e3ULL
#define HASH_P3 0x589965cc75374cc3ULL

/* The 128 bits of a * b: the low half in *a, the high half in *b */
static JSON_INLINE void hash_mum(uint64_t *a, uint64_t *b) {
#if defined(__SIZEOF_INT128__)
    __uint128_t r = (__uint128_t)*a * *b;
    *a = (uint64_t)r;
    *b = (uint64_t)(r >> 64);
#else
    uint64_t ha = *a >> 32, hb = *b >> 32, la = (uint32_t)*a, lb = (uint32_t)*b;
    uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb;
    uint64_t t = rl + (rm0 << 32), c = t < rl;
    uint64_t lo = t + (rm1 << 32);
    c += lo < t;
    *a = lo;
    *b = rh + (rm0 >> 32) + (rm1 >> 32) + c;
#endif
}

static JSON_INLINE uint64_t hash_mix(uint64_t a, uint64_t b) {
    hash_mum(&a, &b);
    return a ^ b;
}

static JSON_INLINE uint64_t hash_r64(const unsigned char *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static JSON_INLINE uint64_t hash_r32(const unsigned char *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static size_t hash_str(const void *key, size_t len) {
    const unsigned char *p = (const unsigned char *)key;
    uint64_t seed = hashtable_seed;
    uint64_t a, b;

    seed *= HASH_P0; /* the 32 bits of the seed spread over the 64 */
    if (len <= 16) {
        if (len >= 4) {
            /* 4..16 bytes: four words of 32 bits, overlapping if need be */
            size_t q = (len >> 3) << 2;
            a = (hash_r32(p) << 32) | hash_r32(p + q);
            b = (hash_r32(p + len - 4) << 32) | hash_r32(p + len - 4 - q);
        } else if (len > 0) {
            a = ((uint64_t)p[0] << 16) | ((uint64_t)p[len >> 1] << 8) | p[len - 1];
            b = 0;
        } else {
            a = 0;
            b = 0;
        }
    } else {
        size_t i = len;
        if (i > 48) {
            uint64_t see1 = seed, see2 = seed;
            do {
                seed = hash_mix(hash_r64(p) ^ HASH_P1, hash_r64(p + 8) ^ seed);
                see1 = hash_mix(hash_r64(p + 16) ^ HASH_P2, hash_r64(p + 24) ^ see1);
                see2 = hash_mix(hash_r64(p + 32) ^ HASH_P3, hash_r64(p + 40) ^ see2);
                p += 48;
                i -= 48;
            } while (i > 48);
            seed ^= see1 ^ see2;
        }
        while (i > 16) {
            seed = hash_mix(hash_r64(p) ^ HASH_P1, hash_r64(p + 8) ^ seed);
            p += 16;
            i -= 16;
        }
        /* the last 16 bytes of the key, overlapping what was hashed */
        a = hash_r64(p + i - 16);
        b = hash_r64(p + i - 8);
    }
    a ^= HASH_P1;
    b ^= seed;
    hash_mum(&a, &b);
    return (size_t)hash_mix(a ^ HASH_P0 ^ len, b ^ HASH_P1);
}

static JSON_INLINE void list_init(list_t *list) {
    list->next = list;
    list->prev = list;
}

static JSON_INLINE void list_insert(list_t *list, list_t *node) {
    node->next = list;
    node->prev = list->prev;
    list->prev->next = node;
    list->prev = node;
}

static JSON_INLINE void list_remove(list_t *list) {
    list->prev->next = list->next;
    list->next->prev = list->prev;
}

static JSON_INLINE int bucket_is_empty(hashtable_t *hashtable, bucket_t *bucket) {
    return bucket->first == &hashtable->list && bucket->first == bucket->last;
}

static void insert_to_bucket(hashtable_t *hashtable, bucket_t *bucket, list_t *list) {
    if (bucket_is_empty(hashtable, bucket)) {
        list_insert(&hashtable->list, list);
        bucket->first = bucket->last = list;
    } else {
        list_insert(bucket->first, list);
        bucket->first = list;
    }
}

static pair_t *hashtable_find_pair(hashtable_t *hashtable, bucket_t *bucket,
                                   const char *key, size_t key_len, size_t hash) {
    list_t *list;
    pair_t *pair;

    if (bucket_is_empty(hashtable, bucket))
        return NULL;

    list = bucket->first;
    while (1) {
        pair = list_to_pair(list);
        if (pair->hash == hash && pair->key_len == key_len &&
            memcmp(pair->key, key, key_len) == 0)
            return pair;

        if (list == bucket->last)
            break;

        list = list->next;
    }

    return NULL;
}

/* returns 0 on success, -1 if key was not found */
static int hashtable_do_del(hashtable_t *hashtable, const char *key, size_t key_len,
                            size_t hash) {
    pair_t *pair;
    bucket_t *bucket;
    size_t index;

    index = hash & hashmask(hashtable->order);
    bucket = &hashtable->buckets[index];

    pair = hashtable_find_pair(hashtable, bucket, key, key_len, hash);
    if (!pair)
        return -1;

    if (&pair->list == bucket->first && &pair->list == bucket->last)
        bucket->first = bucket->last = &hashtable->list;

    else if (&pair->list == bucket->first)
        bucket->first = pair->list.next;

    else if (&pair->list == bucket->last)
        bucket->last = pair->list.prev;

    list_remove(&pair->list);
    list_remove(&pair->ordered_list);
    json_decref(pair->value);

    jsonp_free(pair);
    hashtable->size--;

    return 0;
}

static void hashtable_do_clear(hashtable_t *hashtable) {
    list_t *list, *next;
    pair_t *pair;

    for (list = hashtable->list.next; list != &hashtable->list; list = next) {
        next = list->next;
        pair = list_to_pair(list);
        json_decref(pair->value);
        jsonp_free(pair);
    }
}

static int hashtable_do_rehash(hashtable_t *hashtable) {
    list_t *list, *next;
    pair_t *pair;
    size_t i, index, new_size, new_order;
    struct hashtable_bucket *new_buckets;

    new_order = hashtable->order + 1;
    new_size = hashsize(new_order);

    new_buckets = jsonp_malloc(new_size * sizeof(bucket_t));
    if (!new_buckets)
        return -1;

    jsonp_free(hashtable->buckets);
    hashtable->buckets = new_buckets;
    hashtable->order = new_order;

    for (i = 0; i < new_size; i++) {
        hashtable->buckets[i].first = hashtable->buckets[i].last = &hashtable->list;
    }

    list = hashtable->list.next;
    list_init(&hashtable->list);

    for (; list != &hashtable->list; list = next) {
        next = list->next;
        pair = list_to_pair(list);
        index = pair->hash % new_size;
        insert_to_bucket(hashtable, &hashtable->buckets[index], &pair->list);
    }

    return 0;
}

int hashtable_init(hashtable_t *hashtable) {
    size_t i;

    hashtable->size = 0;
    hashtable->order = INITIAL_HASHTABLE_ORDER;
    hashtable->buckets = jsonp_malloc(hashsize(hashtable->order) * sizeof(bucket_t));
    if (!hashtable->buckets)
        return -1;

    list_init(&hashtable->list);
    list_init(&hashtable->ordered_list);

    for (i = 0; i < hashsize(hashtable->order); i++) {
        hashtable->buckets[i].first = hashtable->buckets[i].last = &hashtable->list;
    }

    return 0;
}

void hashtable_close(hashtable_t *hashtable) {
    hashtable_do_clear(hashtable);
    jsonp_free(hashtable->buckets);
}

static pair_t *init_pair(json_t *value, const char *key, size_t key_len, size_t hash) {
    pair_t *pair;

    /* offsetof(...) returns the size of pair_t without the last,
   flexible member. This way, the correct amount is
   allocated. */

    if (key_len >= (size_t)-1 - offsetof(pair_t, key)) {
        /* Avoid an overflow if the key is very long */
        return NULL;
    }

    pair = jsonp_malloc(offsetof(pair_t, key) + key_len + 1);

    if (!pair)
        return NULL;

    pair->hash = hash;
    memcpy(pair->key, key, key_len);
    pair->key[key_len] = '\0';
    pair->key_len = key_len;
    pair->value = value;

    list_init(&pair->list);
    list_init(&pair->ordered_list);

    return pair;
}

int hashtable_set(hashtable_t *hashtable, const char *key, size_t key_len,
                  json_t *value) {
    pair_t *pair;
    bucket_t *bucket;
    size_t hash, index;

    /* rehash if the load ratio exceeds 1 */
    if (hashtable->size >= hashsize(hashtable->order))
        if (hashtable_do_rehash(hashtable))
            return -1;

    hash = hash_str(key, key_len);
    index = hash & hashmask(hashtable->order);
    bucket = &hashtable->buckets[index];
    pair = hashtable_find_pair(hashtable, bucket, key, key_len, hash);

    if (pair) {
        json_decref(pair->value);
        pair->value = value;
    } else {
        pair = init_pair(value, key, key_len, hash);

        if (!pair)
            return -1;

        insert_to_bucket(hashtable, bucket, &pair->list);
        list_insert(&hashtable->ordered_list, &pair->ordered_list);

        hashtable->size++;
    }
    return 0;
}

void *hashtable_get(hashtable_t *hashtable, const char *key, size_t key_len) {
    pair_t *pair;
    size_t hash;
    bucket_t *bucket;

    hash = hash_str(key, key_len);
    bucket = &hashtable->buckets[hash & hashmask(hashtable->order)];

    pair = hashtable_find_pair(hashtable, bucket, key, key_len, hash);
    if (!pair)
        return NULL;

    return pair->value;
}

int hashtable_del(hashtable_t *hashtable, const char *key, size_t key_len) {
    size_t hash = hash_str(key, key_len);
    return hashtable_do_del(hashtable, key, key_len, hash);
}

void hashtable_clear(hashtable_t *hashtable) {
    size_t i;

    hashtable_do_clear(hashtable);

    for (i = 0; i < hashsize(hashtable->order); i++) {
        hashtable->buckets[i].first = hashtable->buckets[i].last = &hashtable->list;
    }

    list_init(&hashtable->list);
    list_init(&hashtable->ordered_list);
    hashtable->size = 0;
}

void *hashtable_iter(hashtable_t *hashtable) {
    return hashtable_iter_next(hashtable, &hashtable->ordered_list);
}

void *hashtable_iter_at(hashtable_t *hashtable, const char *key, size_t key_len) {
    pair_t *pair;
    size_t hash;
    bucket_t *bucket;

    hash = hash_str(key, key_len);
    bucket = &hashtable->buckets[hash & hashmask(hashtable->order)];

    pair = hashtable_find_pair(hashtable, bucket, key, key_len, hash);
    if (!pair)
        return NULL;

    return &pair->ordered_list;
}

void *hashtable_iter_next(hashtable_t *hashtable, void *iter) {
    list_t *list = (list_t *)iter;
    if (list->next == &hashtable->ordered_list)
        return NULL;
    return list->next;
}

void *hashtable_iter_key(void *iter) {
    pair_t *pair = ordered_list_to_pair((list_t *)iter);
    return pair->key;
}

size_t hashtable_iter_key_len(void *iter) {
    pair_t *pair = ordered_list_to_pair((list_t *)iter);
    return pair->key_len;
}

void *hashtable_iter_value(void *iter) {
    pair_t *pair = ordered_list_to_pair((list_t *)iter);
    return pair->value;
}

void hashtable_iter_set(void *iter, json_t *value) {
    pair_t *pair = ordered_list_to_pair((list_t *)iter);

    json_decref(pair->value);
    pair->value = value;
}
