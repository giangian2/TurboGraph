#include "../include/Hashtable.h"
#include <stdlib.h>
#include <string.h>

#define HT_DEFAULT_CAPACITY 16
#define HT_MAX_LOAD_FACTOR 0.7

enum
{
    SLOT_EMPTY    = 0,
    SLOT_OCCUPIED = 1,
    SLOT_DELETED  = 2
};

static inline void* key_at(const HashTable* table, size_t index)
{
    return (char*)table->keys + index * table->key_size;
}

static inline void* value_at(const HashTable* table, size_t index)
{
    return (char*)table->values + index * table->value_size;
}

// FNV-1a over raw bytes: works for any fixed-size key type, not just strings.
static size_t hash_bytes(const void* data, size_t len)
{
    const unsigned char* bytes = data;
    size_t               hash  = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Returns the index of the occupied slot matching `key`, or
// table->capacity if the key is not present.
static size_t find_slot(const HashTable* table, const void* key)
{
    size_t capacity = table->capacity;
    size_t index    = hash_bytes(key, table->key_size) % capacity;
    size_t probes   = 0;

    while (probes < capacity && table->state[index] != SLOT_EMPTY)
    {
        if (table->state[index] == SLOT_OCCUPIED &&
            memcmp(key_at(table, index), key, table->key_size) == 0)
        {
            return index;
        }
        index = (index + 1) % capacity;
        probes++;
    }

    return capacity;
}

// Inserts key/value into the first empty-or-deleted slot along its
// probe chain. Assumes the key is not already present and that at
// least one such slot exists (callers must grow first).
static void raw_insert(HashTable* table, const void* key, const void* value)
{
    size_t capacity = table->capacity;
    size_t index    = hash_bytes(key, table->key_size) % capacity;

    while (table->state[index] == SLOT_OCCUPIED)
    {
        index = (index + 1) % capacity;
    }

    if (table->state[index] == SLOT_DELETED)
    {
        table->tombstones--;
    }

    memcpy(key_at(table, index), key, table->key_size);
    memcpy(value_at(table, index), value, table->value_size);
    table->state[index] = SLOT_OCCUPIED;
    table->count++;
}

static bool rehash(HashTable* table, size_t new_capacity)
{
    void*          new_keys   = malloc(new_capacity * table->key_size);
    void*          new_values = malloc(new_capacity * table->value_size);
    unsigned char* new_state  = calloc(new_capacity, sizeof(unsigned char));

    if (!new_keys || !new_values || !new_state)
    {
        free(new_keys);
        free(new_values);
        free(new_state);
        return false;
    }

    void*          old_keys     = table->keys;
    void*          old_values   = table->values;
    unsigned char* old_state    = table->state;
    size_t         old_capacity = table->capacity;

    table->keys       = new_keys;
    table->values     = new_values;
    table->state      = new_state;
    table->capacity   = new_capacity;
    table->count      = 0;
    table->tombstones = 0;

    for (size_t i = 0; i < old_capacity; i++)
    {
        if (old_state[i] == SLOT_OCCUPIED)
        {
            raw_insert(table, (char*)old_keys + i * table->key_size,
                       (char*)old_values + i * table->value_size);
        }
    }

    free(old_keys);
    free(old_values);
    free(old_state);
    return true;
}

static bool maybe_grow(HashTable* table)
{
    double load = (double)(table->count + table->tombstones) / (double)table->capacity;
    if (load < HT_MAX_LOAD_FACTOR)
        return true;
    return rehash(table, table->capacity * 2);
}

HashTable* createHashTable(size_t key_size, size_t value_size, size_t initial_capacity)
{
    if (key_size == 0 || value_size == 0)
        return NULL;
    if (initial_capacity == 0)
        initial_capacity = HT_DEFAULT_CAPACITY;

    HashTable* table = malloc(sizeof(HashTable));
    if (!table)
        return NULL;

    table->key_size   = key_size;
    table->value_size = value_size;
    table->capacity   = initial_capacity;
    table->count      = 0;
    table->tombstones = 0;
    table->keys       = malloc(initial_capacity * key_size);
    table->values     = malloc(initial_capacity * value_size);
    table->state      = calloc(initial_capacity, sizeof(unsigned char));

    if (!table->keys || !table->values || !table->state)
    {
        free(table->keys);
        free(table->values);
        free(table->state);
        free(table);
        return NULL;
    }

    return table;
}

void destroyHashTable(HashTable* table)
{
    if (!table)
        return;
    free(table->keys);
    free(table->values);
    free(table->state);
    free(table);
}

bool put(HashTable* table, const void* key, const void* value)
{
    if (!table || !key || !value)
        return false;

    size_t existing = find_slot(table, key);
    if (existing != table->capacity)
    {
        memcpy(value_at(table, existing), value, table->value_size);
        return true;
    }

    if (!maybe_grow(table))
        return false;
    raw_insert(table, key, value);
    return true;
}

void* get(HashTable* table, const void* key)
{
    if (!table || !key)
        return NULL;
    size_t index = find_slot(table, key);
    return index == table->capacity ? NULL : value_at(table, index);
}

bool ht_remove(HashTable* table, const void* key)
{
    if (!table || !key)
        return false;

    size_t index = find_slot(table, key);
    if (index == table->capacity)
        return false;

    table->state[index] = SLOT_DELETED;
    table->count--;
    table->tombstones++;
    return true;
}
