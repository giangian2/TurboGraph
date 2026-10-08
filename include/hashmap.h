#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Generic open addressing hashmap with linear probing, fixed capacity (no realloc),
 * stored behind a "shadow header":
 *
 *   [ HashTable | T slot[capacity] | uint8_t used[capacity] ]
 *                 ^-- pointer handed to the user (T*)
 *
 * T must be a struct with a field named `key`: hashing and equality only look at that
 * field, every other field is the payload. Keys are compared with memcmp, so the key
 * type must have no padding (or be zeroed before being filled).
 *
 *   typedef struct { uint32_t key; uint32_t id; } Entry;
 *   Entry *m = hash_table_create(Entry, 1024, my_hash);
 *   Entry e = { .key = 42, .id = 7 };
 *   hash_table_put(m, e);
 *   Entry *p = hash_table_get(m, 42);
 */

// Receives a pointer to the key field and its size. The table uses the LOW bits of the
// result (hash & (capacity - 1)), so they must be well mixed.
typedef size_t (*hash_func)(const void* key, size_t key_size);

typedef struct
{
    size_t    value_size; // bytes per slot (sizeof(T)); alignas keeps the slots aligned
    size_t    capacity;   // number of slots, always a power of 2
    size_t    count;      // number of occupied slots
    hash_func hash_function;
    uint8_t*  used; // capacity bytes after the slots: 1 = occupied, 0 = free
} HashTable;

// FNV-1a over the key bytes, default hash when NULL is passed to create
static inline size_t hash_bytes(const void* data, size_t len)
{
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t       hash  = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return (size_t)hash;
}

#define hash_table__hdr(h) ((HashTable*)(h) - 1)
// offset in bytes of the key field inside T (offsetof computed from the pointer, nothing is read)
#define hash_table__koff(h) ((size_t)((char*)&(h)->key - (char*)(h)))
#define hash_table__ksize(h) sizeof((h)->key)

// cap is rounded up to a power of 2 (0 = default). Keep the load under ~70%: there is no grow.
#define hash_table_create(T, cap, hash) ((T*)createHashTable(sizeof(T), (cap), (hash)))
#define hash_table_free(h) (destroyHashTable(hash_table__hdr(h)))
#define hash_table_count(h) (hash_table__hdr(h)->count)
#define hash_table_capacity(h) (hash_table__hdr(h)->capacity)
#define hash_table_full(h) (hash_table__hdr(h)->count >= hash_table__hdr(h)->capacity)
#define hash_table_clear(h) (hashTableClear(hash_table__hdr(h)))

// item must be an lvalue of type T. Returns false only if the table is full.
#define hash_table_put(h, item)                                                                    \
    (hashTablePut(hash_table__hdr(h), &((__typeof__(*h)[1]){item}), hash_table__koff(h),           \
                  hash_table__ksize(h)))

// Returns T* to the stored entry or NULL. k may be a struct variable (copied through a one element
// array, like put); a compound literal key needs extra parentheses:
// hash_table_get(m, ((Pair){1, 2})).
#define hash_table_get(h, k)                                                                       \
    ((__typeof__(h))hashTableGet(hash_table__hdr(h), &((__typeof__((h)->key)[1]){k}),              \
                                 hash_table__koff(h), hash_table__ksize(h)))

// Returns true if the key was present.
#define hash_table_remove(h, k)                                                                    \
    (hashTableRemove(hash_table__hdr(h), &((__typeof__((h)->key)[1]){k}), hash_table__koff(h),     \
                     hash_table__ksize(h)))

static inline void* createHashTable(size_t size, size_t initial_capacity, hash_func hash)
{
    if (size == 0)
        return NULL;
    if (initial_capacity == 0)
        initial_capacity = 64;

    size_t capacity = 1;
    while (capacity < initial_capacity)
        capacity <<= 1;

    HashTable* h = (HashTable*)malloc(sizeof(HashTable) + size * capacity + capacity);
    if (h == NULL)
        return NULL;

    h->value_size    = size;
    h->capacity      = capacity;
    h->count         = 0;
    h->hash_function = hash ? hash : hash_bytes;
    h->used          = (uint8_t*)(h + 1) + size * capacity;
    memset(h->used, 0, capacity);

    return h + 1;
}

static inline void destroyHashTable(HashTable* table)
{
    free(table);
}

static inline void hashTableClear(HashTable* table)
{
    memset(table->used, 0, table->capacity);
    table->count = 0;
}

static inline char* hashTable__slot(HashTable* table, size_t i)
{
    return (char*)(table + 1) + i * table->value_size;
}

// Linear probing: index of the slot holding key (*found = true) or of the first free
// slot along the chain (*found = false). SIZE_MAX if the table is full and key is absent.
static inline size_t hashTableFind(HashTable* table, const void* key, size_t key_off,
                                   size_t key_size, bool* found)
{
    size_t mask = table->capacity - 1;
    size_t i    = table->hash_function(key, key_size) & mask;

    for (size_t n = 0; n < table->capacity; n++, i = (i + 1) & mask)
    {
        if (!table->used[i])
        {
            *found = false;
            return i;
        }
        if (memcmp(hashTable__slot(table, i) + key_off, key, key_size) == 0)
        {
            *found = true;
            return i;
        }
    }
    *found = false;
    return SIZE_MAX;
}

// Inserts item, or overwrites the entry with the same key.
static inline bool hashTablePut(HashTable* table, const void* item, size_t key_off, size_t key_size)
{
    bool   found;
    size_t i = hashTableFind(table, (const char*)item + key_off, key_off, key_size, &found);
    if (i == SIZE_MAX)
        return false;

    if (!found)
    {
        table->used[i] = 1;
        table->count++;
    }
    memcpy(hashTable__slot(table, i), item, table->value_size);
    return true;
}

// Pointer to the stored entry, or NULL. Stays valid until the entry is removed
// (there is no rehash), but a remove can shift other entries back in the chain.
static inline void* hashTableGet(HashTable* table, const void* key, size_t key_off, size_t key_size)
{
    bool   found;
    size_t i = hashTableFind(table, key, key_off, key_size, &found);
    return found ? hashTable__slot(table, i) : NULL;
}

// Backward shift deletion: no tombstones, the entries after the hole that would become
// unreachable are moved back into it.
static inline bool hashTableRemove(HashTable* table, const void* key, size_t key_off,
                                   size_t key_size)
{
    bool   found;
    size_t hole = hashTableFind(table, key, key_off, key_size, &found);
    if (!found)
        return false;

    size_t mask = table->capacity - 1;
    size_t j    = hole;
    // bounded: on a 100% full table there is no free slot to stop at
    for (size_t n = 1; n < table->capacity; n++)
    {
        j = (j + 1) & mask;
        if (!table->used[j])
            break;

        size_t home = table->hash_function(hashTable__slot(table, j) + key_off, key_size) & mask;
        // the entry at j can fill the hole only if its home is not in (hole, j] (cyclically)
        bool home_between = (hole <= j) ? (hole < home && home <= j) : (hole < home || home <= j);
        if (!home_between)
        {
            memcpy(hashTable__slot(table, hole), hashTable__slot(table, j), table->value_size);
            hole = j;
        }
    }
    table->used[hole] = 0;
    table->count--;
    return true;
}

#endif
