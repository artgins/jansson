/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include <jansson_private_config.h>
#endif

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include "hashtable.h"
#include "jansson.h"
#include "jansson_private.h"
#include "utf.h"

/* Work around nonstandard isnan() and isinf() implementations */
#ifndef isnan
#ifndef __sun
static JSON_INLINE int isnan(double x) { return x != x; }
#endif
#endif
#ifndef isinf
static JSON_INLINE int isinf(double x) { return !isnan(x) && isnan(x - x); }
#endif

static int do_equal(const json_t *json1, const json_t *json2, int depth);
json_t *do_deep_copy(const json_t *json, loop_path_t *parents, int depth);

static JSON_INLINE void json_init(json_t *json, json_type type) {
    json->type = type;
    json->refcount = 1;
}

/* The key of a container in the table of its parents: what glibc's
   snprintf(key, key_size, "%p", json) writes ("0x" and the hexadecimal
   digits of the address, without leading zeros), without a printf. It is
   made for every object and array dumped, deep-copied or compared. A buffer
   too short for it, or a NULL json, still goes to snprintf(). */
static size_t loop_key(char *key, size_t key_size, const json_t *json) {
    static const char hex[] = "0123456789abcdef";
    char digits[sizeof(uintptr_t) * 2];
    uintptr_t value = (uintptr_t)json;
    size_t n = 0;

    while (value) {
        digits[n++] = hex[value & 0xf];
        value >>= 4;
    }
    if (n == 0 || n + 3 > key_size)
        return snprintf(key, key_size, "%p", json);

    key[0] = '0';
    key[1] = 'x';
    for (size_t i = 0; i < n; i++)
        key[2 + i] = digits[n - 1 - i];
    key[2 + n] = '\0';
    return 2 + n;
}

void jsonp_loop_init(loop_path_t *path) {
    path->depth = 0;
    path->deep_init = 0;
}

void jsonp_loop_close(loop_path_t *path) {
    if (path->deep_init)
        hashtable_close(&path->deep);
}

int jsonp_loop_check(loop_path_t *path, const json_t *json) {
    char key[LOOP_KEY_LEN];
    size_t i, key_len;
    size_t n = path->depth < LOOP_PATH_SHALLOW ? path->depth : LOOP_PATH_SHALLOW;

    for (i = 0; i < n; i++) {
        if (path->shallow[i] == json)
            return -1;
    }
    if (path->depth < LOOP_PATH_SHALLOW) {
        path->shallow[path->depth++] = json;
        return 0;
    }

    if (!path->deep_init) {
        if (hashtable_init(&path->deep))
            return -1;
        path->deep_init = 1;
    }
    key_len = loop_key(key, sizeof(key), json);
    if (hashtable_get(&path->deep, key, key_len))
        return -1;
    if (hashtable_set(&path->deep, key, key_len, json_null()))
        return -1;
    path->depth++;
    return 0;
}

void jsonp_loop_leave(loop_path_t *path, const json_t *json) {
    path->depth--;
    if (path->depth >= LOOP_PATH_SHALLOW) {
        char key[LOOP_KEY_LEN];
        hashtable_del(&path->deep, key, loop_key(key, sizeof(key), json));
    }
}

/*** object ***/

extern volatile uint32_t hashtable_seed;

json_t *json_object(void) {
    json_object_t *object = jsonp_malloc(sizeof(json_object_t));
    if (!object)
        return NULL;

    if (!hashtable_seed) {
        /* Autoseed */
        json_object_seed(0);
    }

    json_init(&object->json, JSON_OBJECT);

    if (hashtable_init(&object->hashtable)) {
        jsonp_free(object);
        return NULL;
    }

    return &object->json;
}

static void json_delete_object(json_object_t *object) {
    hashtable_close(&object->hashtable);
    jsonp_free(object);
}

size_t json_object_size(const json_t *json) {
    json_object_t *object;

    if (!json_is_object(json))
        return 0;

    object = json_to_object(json);
    return object->hashtable.size;
}

json_t *json_object_get(const json_t *json, const char *key) {
    if (!key)
        return NULL;

    return json_object_getn(json, key, strlen(key));
}

json_t *json_object_getn(const json_t *json, const char *key, size_t key_len) {
    json_object_t *object;

    if (!key || !json_is_object(json))
        return NULL;

    object = json_to_object(json);
    return hashtable_get(&object->hashtable, key, key_len);
}

int json_object_set_new_nocheck(json_t *json, const char *key, json_t *value) {
    if (!key) {
        json_decref(value);
        return -1;
    }
    return json_object_setn_new_nocheck(json, key, strlen(key), value);
}

int json_object_setn_new_nocheck(json_t *json, const char *key, size_t key_len,
                                 json_t *value) {
    json_object_t *object;

    if (!value)
        return -1;

    if (!key || !json_is_object(json) || json == value) {
        json_decref(value);
        return -1;
    }
    object = json_to_object(json);

    if (hashtable_set(&object->hashtable, key, key_len, value)) {
        json_decref(value);
        return -1;
    }

    return 0;
}

int json_object_set_new(json_t *json, const char *key, json_t *value) {
    if (!key) {
        json_decref(value);
        return -1;
    }

    return json_object_setn_new(json, key, strlen(key), value);
}

int json_object_setn_new(json_t *json, const char *key, size_t key_len, json_t *value) {
    if (!key || !utf8_check_string(key, key_len)) {
        json_decref(value);
        return -1;
    }

    return json_object_setn_new_nocheck(json, key, key_len, value);
}

int json_object_del(json_t *json, const char *key) {
    if (!key)
        return -1;

    return json_object_deln(json, key, strlen(key));
}

int json_object_deln(json_t *json, const char *key, size_t key_len) {
    json_object_t *object;

    if (!key || !json_is_object(json))
        return -1;

    object = json_to_object(json);
    return hashtable_del(&object->hashtable, key, key_len);
}

int json_object_clear(json_t *json) {
    json_object_t *object;

    if (!json_is_object(json))
        return -1;

    object = json_to_object(json);
    hashtable_clear(&object->hashtable);

    return 0;
}

int json_object_update(json_t *object, json_t *other) {
    const char *key;
    size_t key_len;
    json_t *value;

    if (!json_is_object(object) || !json_is_object(other))
        return -1;

    json_object_keylen_foreach(other, key, key_len, value) {
        if (json_object_setn_nocheck(object, key, key_len, value))
            return -1;
    }

    return 0;
}

int json_object_update_existing(json_t *object, json_t *other) {
    const char *key;
    size_t key_len;
    json_t *value;

    if (!json_is_object(object) || !json_is_object(other))
        return -1;

    json_object_keylen_foreach(other, key, key_len, value) {
        if (json_object_getn(object, key, key_len))
            json_object_setn_nocheck(object, key, key_len, value);
    }

    return 0;
}

int json_object_update_missing(json_t *object, json_t *other) {
    const char *key;
    size_t key_len;
    json_t *value;

    if (!json_is_object(object) || !json_is_object(other))
        return -1;

    json_object_keylen_foreach(other, key, key_len, value) {
        if (!json_object_getn(object, key, key_len))
            json_object_setn_nocheck(object, key, key_len, value);
    }

    return 0;
}

int do_object_update_recursive(json_t *object, json_t *other, loop_path_t *parents) {
    const char *key;
    size_t key_len;
    json_t *value;
    int res = 0;

    if (!json_is_object(object) || !json_is_object(other))
        return -1;

    if (jsonp_loop_check(parents, other))
        return -1;

    json_object_keylen_foreach(other, key, key_len, value) {
        json_t *v = json_object_getn(object, key, key_len);

        if (json_is_object(v) && json_is_object(value)) {
            if (do_object_update_recursive(v, value, parents)) {
                res = -1;
                break;
            }
        } else {
            if (json_object_setn_nocheck(object, key, key_len, value)) {
                res = -1;
                break;
            }
        }
    }

    jsonp_loop_leave(parents, other);

    return res;
}

int json_object_update_recursive(json_t *object, json_t *other) {
    int res;
    loop_path_t parents_set;

    jsonp_loop_init(&parents_set);
    res = do_object_update_recursive(object, other, &parents_set);
    jsonp_loop_close(&parents_set);

    return res;
}

void *json_object_iter(json_t *json) {
    json_object_t *object;

    if (!json_is_object(json))
        return NULL;

    object = json_to_object(json);
    return hashtable_iter(&object->hashtable);
}

void *json_object_iter_at(json_t *json, const char *key) {
    json_object_t *object;

    if (!key || !json_is_object(json))
        return NULL;

    object = json_to_object(json);
    return hashtable_iter_at(&object->hashtable, key, strlen(key));
}

void *json_object_iter_next(json_t *json, void *iter) {
    json_object_t *object;

    if (!json_is_object(json) || iter == NULL)
        return NULL;

    object = json_to_object(json);
    return hashtable_iter_next(&object->hashtable, iter);
}

const char *json_object_iter_key(void *iter) {
    if (!iter)
        return NULL;

    return hashtable_iter_key(iter);
}

size_t json_object_iter_key_len(void *iter) {
    if (!iter)
        return 0;

    return hashtable_iter_key_len(iter);
}

json_t *json_object_iter_value(void *iter) {
    if (!iter)
        return NULL;

    return (json_t *)hashtable_iter_value(iter);
}

int json_object_iter_set_new(json_t *json, void *iter, json_t *value) {
    if (!json_is_object(json) || !iter || !value) {
        json_decref(value);
        return -1;
    }

    hashtable_iter_set(iter, value);
    return 0;
}

void *json_object_key_to_iter(const char *key) {
    if (!key)
        return NULL;

    return hashtable_key_to_iter(key);
}

static int json_object_equal(const json_t *object1, const json_t *object2, int depth) {
    const char *key;
    size_t key_len;
    const json_t *value1, *value2;

    if (json_object_size(object1) != json_object_size(object2))
        return 0;

    json_object_keylen_foreach((json_t *)object1, key, key_len, value1) {
        value2 = json_object_getn(object2, key, key_len);

        if (!do_equal(value1, value2, depth + 1))
            return 0;
    }

    return 1;
}

static json_t *json_object_copy(json_t *object) {
    json_t *result;

    const char *key;
    size_t key_len;
    json_t *value;

    result = json_object();
    if (!result)
        return NULL;

    json_object_keylen_foreach(object, key, key_len, value)
        json_object_setn_nocheck(result, key, key_len, value);

    return result;
}

static json_t *json_object_deep_copy(const json_t *object, loop_path_t *parents,
                                     int depth) {
    json_t *result;
    void *iter;

    if (jsonp_loop_check(parents, object))
        return NULL;

    result = json_object();
    if (!result)
        goto out;

    /* Cannot use json_object_foreach because object has to be cast
       non-const */
    iter = json_object_iter((json_t *)object);
    while (iter) {
        const char *key;
        size_t key_len;
        const json_t *value;
        key = json_object_iter_key(iter);
        key_len = json_object_iter_key_len(iter);
        value = json_object_iter_value(iter);

        if (json_object_setn_new_nocheck(result, key, key_len,
                                         do_deep_copy(value, parents, depth + 1))) {
            json_decref(result);
            result = NULL;
            break;
        }
        iter = json_object_iter_next((json_t *)object, iter);
    }

out:
    jsonp_loop_leave(parents, object);

    return result;
}

/*** array ***/

json_t *json_array(void) {
    json_array_t *array = jsonp_malloc(sizeof(json_array_t));
    if (!array)
        return NULL;
    json_init(&array->json, JSON_ARRAY);

    array->entries = 0;
    array->size = 8;

    array->table = jsonp_malloc(array->size * sizeof(json_t *));
    if (!array->table) {
        jsonp_free(array);
        return NULL;
    }

    return &array->json;
}

static void json_delete_array(json_array_t *array) {
    size_t i;

    for (i = 0; i < array->entries; i++)
        json_decref(array->table[i]);

    jsonp_free(array->table);
    jsonp_free(array);
}

size_t json_array_size(const json_t *json) {
    if (!json_is_array(json))
        return 0;

    return json_to_array(json)->entries;
}

json_t *json_array_get(const json_t *json, size_t index) {
    json_array_t *array;
    if (!json_is_array(json))
        return NULL;
    array = json_to_array(json);

    if (index >= array->entries)
        return NULL;

    return array->table[index];
}

int json_array_set_new(json_t *json, size_t index, json_t *value) {
    json_array_t *array;

    if (!value)
        return -1;

    if (!json_is_array(json) || json == value) {
        json_decref(value);
        return -1;
    }
    array = json_to_array(json);

    if (index >= array->entries) {
        json_decref(value);
        return -1;
    }

    json_decref(array->table[index]);
    array->table[index] = value;

    return 0;
}

static void array_move(json_array_t *array, size_t dest, size_t src, size_t count) {
    memmove(&array->table[dest], &array->table[src], count * sizeof(json_t *));
}

static void array_copy(json_t **dest, size_t dpos, json_t **src, size_t spos,
                       size_t count) {
    memcpy(&dest[dpos], &src[spos], count * sizeof(json_t *));
}

static json_t **json_array_grow(json_array_t *array, size_t amount) {
    size_t new_size;
    json_t **old_table, **new_table;

    if (array->entries + amount <= array->size)
        return array->table;

    old_table = array->table;

    new_size = max(array->size + amount, array->size * 2);
    new_table = jsonp_realloc(old_table, array->size * sizeof(json_t *),
                              new_size * sizeof(json_t *));
    if (!new_table)
        return NULL;

    array->size = new_size;
    array->table = new_table;

    return array->table;
}

int json_array_append_new(json_t *json, json_t *value) {
    json_array_t *array;

    if (!value)
        return -1;

    if (!json_is_array(json) || json == value) {
        json_decref(value);
        return -1;
    }
    array = json_to_array(json);

    if (!json_array_grow(array, 1)) {
        json_decref(value);
        return -1;
    }

    array->table[array->entries] = value;
    array->entries++;

    return 0;
}

int json_array_insert_new(json_t *json, size_t index, json_t *value) {
    json_array_t *array;

    if (!value)
        return -1;

    if (!json_is_array(json) || json == value) {
        json_decref(value);
        return -1;
    }
    array = json_to_array(json);

    if (index > array->entries) {
        json_decref(value);
        return -1;
    }

    if (!json_array_grow(array, 1)) {
        json_decref(value);
        return -1;
    }
    if (index != array->entries)
        array_move(array, index + 1, index, array->entries - index);

    array->table[index] = value;
    array->entries++;

    return 0;
}

int json_array_remove(json_t *json, size_t index) {
    json_array_t *array;

    if (!json_is_array(json))
        return -1;
    array = json_to_array(json);

    if (index >= array->entries)
        return -1;

    json_decref(array->table[index]);

    /* If we're removing the last element, nothing has to be moved */
    if (index < array->entries - 1)
        array_move(array, index, index + 1, array->entries - index - 1);

    array->entries--;

    return 0;
}

int json_array_clear(json_t *json) {
    json_array_t *array;
    size_t i;

    if (!json_is_array(json))
        return -1;
    array = json_to_array(json);

    for (i = 0; i < array->entries; i++)
        json_decref(array->table[i]);

    array->entries = 0;
    return 0;
}

int json_array_extend(json_t *json, json_t *other_json) {
    json_array_t *array, *other;
    size_t i;

    if (!json_is_array(json) || !json_is_array(other_json))
        return -1;
    array = json_to_array(json);
    other = json_to_array(other_json);

    if (!json_array_grow(array, other->entries))
        return -1;

    for (i = 0; i < other->entries; i++)
        json_incref(other->table[i]);

    array_copy(array->table, array->entries, other->table, 0, other->entries);

    array->entries += other->entries;
    return 0;
}

static int json_array_equal(const json_t *array1, const json_t *array2, int depth) {
    size_t i, size;

    size = json_array_size(array1);
    if (size != json_array_size(array2))
        return 0;

    for (i = 0; i < size; i++) {
        json_t *value1, *value2;

        value1 = json_array_get(array1, i);
        value2 = json_array_get(array2, i);

        if (!do_equal(value1, value2, depth + 1))
            return 0;
    }

    return 1;
}

static json_t *json_array_copy(json_t *array) {
    json_t *result;
    size_t i;

    result = json_array();
    if (!result)
        return NULL;

    for (i = 0; i < json_array_size(array); i++)
        json_array_append(result, json_array_get(array, i));

    return result;
}

static json_t *json_array_deep_copy(const json_t *array, loop_path_t *parents,
                                    int depth) {
    json_t *result;
    size_t i;

    if (jsonp_loop_check(parents, array))
        return NULL;

    result = json_array();
    if (!result)
        goto out;

    for (i = 0; i < json_array_size(array); i++) {
        if (json_array_append_new(
                result, do_deep_copy(json_array_get(array, i), parents, depth + 1))) {
            json_decref(result);
            result = NULL;
            break;
        }
    }

out:
    jsonp_loop_leave(parents, array);

    return result;
}

/*** string ***/

/* A string whose value fits, with its json_string_t and the NUL, in a block
   of STRING_INLINE_SIZE bytes is made with one allocation: the value right
   after the json_string_t. Any other value has an allocation of its own:
   one handed over by its owner (jsonp_stringn_nocheck_own()), a longer
   one, and every value given later by json_string_set(). */
#define STRING_INLINE_SIZE 1024

static JSON_INLINE char *string_inline_value(json_string_t *string) {
    return (char *)(string + 1);
}

static void string_free_value(json_string_t *string) {
    if (string->value != string_inline_value(string))
        jsonp_free(string->value);
}

static json_t *string_create(const char *value, size_t len, int own) {
    char *v;
    json_string_t *string;

    if (!value)
        return NULL;

    if (!own && len < STRING_INLINE_SIZE - sizeof(json_string_t)) {
        string = jsonp_malloc(sizeof(json_string_t) + len + 1);
        if (!string)
            return NULL;
        v = string_inline_value(string);
        memcpy(v, value, len);
        v[len] = '\0';
        json_init(&string->json, JSON_STRING);
        string->value = v;
        string->length = len;
        return &string->json;
    }

    if (own)
        v = (char *)value;
    else {
        v = jsonp_strndup(value, len);
        if (!v)
            return NULL;
    }

    string = jsonp_malloc(sizeof(json_string_t));
    if (!string) {
        jsonp_free(v);
        return NULL;
    }
    json_init(&string->json, JSON_STRING);
    string->value = v;
    string->length = len;

    return &string->json;
}

json_t *json_string_nocheck(const char *value) {
    if (!value)
        return NULL;

    return string_create(value, strlen(value), 0);
}

json_t *json_stringn_nocheck(const char *value, size_t len) {
    return string_create(value, len, 0);
}

/* this is private; "steal" is not a public API concept */
json_t *jsonp_stringn_nocheck_own(const char *value, size_t len) {
    return string_create(value, len, 1);
}

json_t *json_string(const char *value) {
    if (!value)
        return NULL;

    return json_stringn(value, strlen(value));
}

json_t *json_stringn(const char *value, size_t len) {
    if (!value || !utf8_check_string(value, len))
        return NULL;

    return json_stringn_nocheck(value, len);
}

const char *json_string_value(const json_t *json) {
    if (!json_is_string(json))
        return NULL;

    return json_to_string(json)->value;
}

size_t json_string_length(const json_t *json) {
    if (!json_is_string(json))
        return 0;

    return json_to_string(json)->length;
}

int json_string_set_nocheck(json_t *json, const char *value) {
    if (!value)
        return -1;

    return json_string_setn_nocheck(json, value, strlen(value));
}

int json_string_setn_nocheck(json_t *json, const char *value, size_t len) {
    char *dup;
    json_string_t *string;

    if (!json_is_string(json) || !value)
        return -1;

    dup = jsonp_strndup(value, len);
    if (!dup)
        return -1;

    string = json_to_string(json);
    string_free_value(string);
    string->value = dup;
    string->length = len;

    return 0;
}

int json_string_set(json_t *json, const char *value) {
    if (!value)
        return -1;

    return json_string_setn(json, value, strlen(value));
}

int json_string_setn(json_t *json, const char *value, size_t len) {
    if (!value || !utf8_check_string(value, len))
        return -1;

    return json_string_setn_nocheck(json, value, len);
}

static void json_delete_string(json_string_t *string) {
    string_free_value(string);
    jsonp_free(string);
}

static int json_string_equal(const json_t *string1, const json_t *string2) {
    json_string_t *s1, *s2;

    s1 = json_to_string(string1);
    s2 = json_to_string(string2);
    return s1->length == s2->length && !memcmp(s1->value, s2->value, s1->length);
}

static json_t *json_string_copy(const json_t *string) {
    json_string_t *s;

    s = json_to_string(string);
    return json_stringn_nocheck(s->value, s->length);
}

json_t *json_vsprintf(const char *fmt, va_list ap) {
    json_t *json = NULL;
    int length;
    char *buf;
    va_list aq;
    va_copy(aq, ap);

    length = vsnprintf(NULL, 0, fmt, ap);
    if (length < 0)
        goto out;
    if (length == 0) {
        json = json_string("");
        goto out;
    }

    buf = jsonp_malloc((size_t)length + 1);
    if (!buf)
        goto out;

    vsnprintf(buf, (size_t)length + 1, fmt, aq);
    if (!utf8_check_string(buf, length)) {
        jsonp_free(buf);
        goto out;
    }

    json = jsonp_stringn_nocheck_own(buf, length);

out:
    va_end(aq);
    return json;
}

json_t *json_sprintf(const char *fmt, ...) {
    json_t *result;
    va_list ap;

    va_start(ap, fmt);
    result = json_vsprintf(fmt, ap);
    va_end(ap);

    return result;
}

/*** integer ***/

json_t *json_integer(json_int_t value) {
    json_integer_t *integer = jsonp_malloc(sizeof(json_integer_t));
    if (!integer)
        return NULL;
    json_init(&integer->json, JSON_INTEGER);

    integer->value = value;
    return &integer->json;
}

json_int_t json_integer_value(const json_t *json) {
    if (!json_is_integer(json))
        return 0;

    return json_to_integer(json)->value;
}

int json_integer_set(json_t *json, json_int_t value) {
    if (!json_is_integer(json))
        return -1;

    json_to_integer(json)->value = value;

    return 0;
}

static void json_delete_integer(json_integer_t *integer) { jsonp_free(integer); }

static int json_integer_equal(const json_t *integer1, const json_t *integer2) {
    return json_integer_value(integer1) == json_integer_value(integer2);
}

static json_t *json_integer_copy(const json_t *integer) {
    return json_integer(json_integer_value(integer));
}

/*** real ***/

json_t *json_real(double value) {
    json_real_t *real;

    if (isnan(value) || isinf(value))
        return NULL;

    real = jsonp_malloc(sizeof(json_real_t));
    if (!real)
        return NULL;
    json_init(&real->json, JSON_REAL);

    real->value = value;
    return &real->json;
}

double json_real_value(const json_t *json) {
    if (!json_is_real(json))
        return 0;

    return json_to_real(json)->value;
}

int json_real_set(json_t *json, double value) {
    if (!json_is_real(json) || isnan(value) || isinf(value))
        return -1;

    json_to_real(json)->value = value;

    return 0;
}

static void json_delete_real(json_real_t *real) { jsonp_free(real); }

static int json_real_equal(const json_t *real1, const json_t *real2) {
    return json_real_value(real1) == json_real_value(real2);
}

static json_t *json_real_copy(const json_t *real) {
    return json_real(json_real_value(real));
}

/*** number ***/

double json_number_value(const json_t *json) {
    if (json_is_integer(json))
        return (double)json_integer_value(json);
    else if (json_is_real(json))
        return json_real_value(json);
    else
        return 0.0;
}

/*** simple values ***/

json_t *json_true(void) {
    static json_t the_true = {JSON_TRUE, (size_t)-1};
    return &the_true;
}

json_t *json_false(void) {
    static json_t the_false = {JSON_FALSE, (size_t)-1};
    return &the_false;
}

json_t *json_null(void) {
    static json_t the_null = {JSON_NULL, (size_t)-1};
    return &the_null;
}

/*** deletion ***/

void json_delete(json_t *json) {
    if (!json)
        return;

    switch (json_typeof(json)) {
        case JSON_OBJECT:
            json_delete_object(json_to_object(json));
            break;
        case JSON_ARRAY:
            json_delete_array(json_to_array(json));
            break;
        case JSON_STRING:
            json_delete_string(json_to_string(json));
            break;
        case JSON_INTEGER:
            json_delete_integer(json_to_integer(json));
            break;
        case JSON_REAL:
            json_delete_real(json_to_real(json));
            break;
        default:
            return;
    }

    /* json_delete is not called for true, false or null */
}

/*** equality ***/

int json_equal(const json_t *json1, const json_t *json2) {
    return do_equal(json1, json2, 0);
}

static int do_equal(const json_t *json1, const json_t *json2, int depth) {
    if (!json1 || !json2)
        return 0;

    if (json_typeof(json1) != json_typeof(json2))
        return 0;

    /* this covers true, false and null as they are singletons */
    if (json1 == json2)
        return 1;

    if (depth >= JSON_PARSER_MAX_DEPTH)
        return 0;

    switch (json_typeof(json1)) {
        case JSON_OBJECT:
            return json_object_equal(json1, json2, depth);
        case JSON_ARRAY:
            return json_array_equal(json1, json2, depth);
        case JSON_STRING:
            return json_string_equal(json1, json2);
        case JSON_INTEGER:
            return json_integer_equal(json1, json2);
        case JSON_REAL:
            return json_real_equal(json1, json2);
        default:
            return 0;
    }
}

/*** copying ***/

json_t *json_copy(json_t *json) {
    if (!json)
        return NULL;

    switch (json_typeof(json)) {
        case JSON_OBJECT:
            return json_object_copy(json);
        case JSON_ARRAY:
            return json_array_copy(json);
        case JSON_STRING:
            return json_string_copy(json);
        case JSON_INTEGER:
            return json_integer_copy(json);
        case JSON_REAL:
            return json_real_copy(json);
        case JSON_TRUE:
        case JSON_FALSE:
        case JSON_NULL:
            return json;
        default:
            return NULL;
    }
}

json_t *json_deep_copy(const json_t *json) {
    json_t *res;
    loop_path_t parents_set;

    jsonp_loop_init(&parents_set);
    res = do_deep_copy(json, &parents_set, 0);
    jsonp_loop_close(&parents_set);

    return res;
}

json_t *do_deep_copy(const json_t *json, loop_path_t *parents, int depth) {
    if (!json)
        return NULL;

    if (depth >= JSON_PARSER_MAX_DEPTH)
        return NULL;

    switch (json_typeof(json)) {
        case JSON_OBJECT:
            return json_object_deep_copy(json, parents, depth);
        case JSON_ARRAY:
            return json_array_deep_copy(json, parents, depth);
            /* for the rest of the types, deep copying doesn't differ from
               shallow copying */
        case JSON_STRING:
            return json_string_copy(json);
        case JSON_INTEGER:
            return json_integer_copy(json);
        case JSON_REAL:
            return json_real_copy(json);
        case JSON_TRUE:
        case JSON_FALSE:
        case JSON_NULL:
            return (json_t *)json;
        default:
            return NULL;
    }
}
