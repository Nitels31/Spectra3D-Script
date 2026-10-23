// Spectra3D Script - dictionary (hash map with open addressing)
//
// Keys are strings or integers, values are any value. The table grows
// automatically and never shrinks, so lookups stay O(1).

#include "dict.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DICT_EMPTY  0
#define DICT_USED   1
#define DICT_DELETED 2

static uint32_t hash_int(int64_t value) {
    uint32_t hash = (uint32_t)value;
    hash ^= hash >> 16;
    hash *= 0x7feb352d;
    hash ^= hash >> 15;
    hash *= 0x846ca68b;
    hash ^= hash >> 16;
    return hash;
}

// Returns false when the key type cannot be used as a dictionary key.
static bool dict_hash_of(Value key, uint32_t* out) {
    switch (key.type) {
        case VAL_STRING:
            *out = ((ObjString*)key.as.obj)->hash;
            return true;
        case VAL_INT:
            *out = hash_int(key.as.integer);
            return true;
        case VAL_BOOL:
            *out = hash_int(key.as.boolean ? 1 : 0);
            return true;
        default:
            return false;
    }
}

static bool dict_key_equal(Value a, Value b) {
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        return values_equal(a, b);
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return a.as.integer == b.as.integer;
    }
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) {
        return a.as.boolean == b.as.boolean;
    }
    return false;
}

ObjDict* dict_new(VM* vm) {
    ObjDict* dict = (ObjDict*)malloc(sizeof(ObjDict));
    if (!dict) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    dict->obj.type = OBJ_DICT;
    dict->obj.next = vm->objects;
    vm->objects = &dict->obj;
    dict->count = 0;
    dict->deleted = 0;
    dict->capacity = 8;
    dict->entries = (DictEntry*)calloc(dict->capacity, sizeof(DictEntry));
    if (!dict->entries) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    mem_account(vm, sizeof(ObjDict) + sizeof(DictEntry) * dict->capacity);
    return dict;
}

static void dict_rehash(VM* vm, ObjDict* dict, uint32_t capacity) {
    DictEntry* old_entries = dict->entries;
    uint32_t old_capacity = dict->capacity;

    dict->entries = (DictEntry*)calloc(capacity, sizeof(DictEntry));
    if (!dict->entries) {
        fprintf(stderr, "sps: out of memory\n");
        exit(1);
    }
    dict->capacity = capacity;
    dict->count = 0;
    dict->deleted = 0;

    for (uint32_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].state == DICT_USED) {
            dict_set(vm, dict, old_entries[i].key, old_entries[i].value);
        }
    }
    free(old_entries);
}

static uint32_t dict_find_slot(DictEntry* entries, uint32_t capacity, Value key, uint32_t hash,
                               bool* found) {
    uint32_t mask = capacity - 1;
    uint32_t index = hash & mask;
    uint32_t first_deleted = 0;
    bool have_deleted = false;

    for (;;) {
        DictEntry* entry = &entries[index];
        if (entry->state == DICT_EMPTY) {
            *found = false;
            return have_deleted ? first_deleted : index;
        }
        if (entry->state == DICT_DELETED) {
            if (!have_deleted) {
                first_deleted = index;
                have_deleted = true;
            }
        } else if (dict_key_equal(entry->key, key)) {
            *found = true;
            return index;
        }
        index = (index + 1) & mask;
    }
}

bool dict_get(ObjDict* dict, Value key, Value* out) {
    uint32_t hash;
    if (!dict_hash_of(key, &hash)) return false;
    bool found = false;
    uint32_t index = dict_find_slot(dict->entries, dict->capacity, key, hash, &found);
    if (!found) return false;
    if (out) *out = dict->entries[index].value;
    return true;
}

void dict_set(VM* vm, ObjDict* dict, Value key, Value value) {
    uint32_t hash;
    if (!dict_hash_of(key, &hash)) {
        vm_runtime_error(vm, 0, "unhashable key type");
        return;
    }

    if ((dict->count + dict->deleted + 1) * 4 >= dict->capacity * 3) {
        uint32_t capacity = dict->capacity;
        if ((dict->count + 1) * 4 >= capacity * 3) {
            capacity *= 2;
        } else {
            capacity = dict->capacity; // there are enough free slots, just rehash
        }
        dict_rehash(vm, dict, capacity);
    }

    bool found = false;
    uint32_t index = dict_find_slot(dict->entries, dict->capacity, key, hash, &found);
    DictEntry* entry = &dict->entries[index];
    if (found) {
        entry->value = value;
        return;
    }
    if (entry->state == DICT_DELETED) dict->deleted--;
    entry->state = DICT_USED;
    entry->key = key;
    entry->value = value;
    dict->count++;
}

bool dict_remove(ObjDict* dict, Value key) {
    uint32_t hash;
    if (!dict_hash_of(key, &hash)) return false;
    bool found = false;
    uint32_t index = dict_find_slot(dict->entries, dict->capacity, key, hash, &found);
    if (!found) return false;
    dict->entries[index].state = DICT_DELETED;
    dict->entries[index].key = value_nil();
    dict->entries[index].value = value_nil();
    dict->count--;
    dict->deleted++;
    return true;
}

void dict_clear(ObjDict* dict) {
    memset(dict->entries, 0, sizeof(DictEntry) * dict->capacity);
    dict->count = 0;
    dict->deleted = 0;
}
