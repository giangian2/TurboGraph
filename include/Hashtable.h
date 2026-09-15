#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Generic, dynamically-growing hash table with open addressing
     * (linear probing) and a Structure-of-Arrays layout: keys, values
     * and slot states each live in their own contiguous, realloc-able
     * buffer, so scanning just the keys (or just the values) stays
     * cache-friendly.
     *
     * Key and value TYPES are fixed per table: you specify their size
     * in bytes once, at creation time (sizeof(YourKeyType), sizeof(Your
     * ValueType)), and every put()/get()/ht_remove() call is expected
     * to pass a pointer to exactly that many bytes.
     *
     * Keys/values are hashed and compared as raw bytes (FNV-1a / memcmp),
     * NOT as null-terminated strings. If you use a fixed-size char buffer
     * as a key, zero the whole buffer (e.g. memset then strncpy) before
     * every put()/get(): two "equal" strings with different trailing
     * garbage bytes will otherwise hash/compare as different keys.
     */

    typedef struct
    {
        size_t         key_size;   // bytes per key, fixed at creation
        size_t         value_size; // bytes per value, fixed at creation
        size_t         capacity;   // number of slots currently allocated
        size_t         count;      // number of occupied slots
        size_t         tombstones; // deleted slots not yet reclaimed by an insert/rehash
        void*          keys;       // capacity * key_size bytes
        void*          values;     // capacity * value_size bytes
        unsigned char* state;      // capacity bytes: SLOT_EMPTY / SLOT_OCCUPIED / SLOT_DELETED
    } HashTable;

    // initial_capacity == 0 uses a sensible default. Returns NULL on
    // invalid sizes (0) or allocation failure.
    HashTable* createHashTable(size_t key_size, size_t value_size, size_t initial_capacity);
    void       destroyHashTable(HashTable* table);

    // Inserts a new key/value pair, or overwrites the value if the key
    // is already present. Returns false only on allocation failure
    // during a grow.
    bool put(HashTable* table, const void* key, const void* value);

    // Returns a pointer to the stored value, or NULL if the key isn't
    // present. The pointer is invalidated by any later put()/ht_remove()
    // that triggers a rehash, so copy the value out if it needs to
    // outlive the next mutating call !!!.
    void* get(HashTable* table, const void* key);

    // Removes a key. Returns true if it was present.
    bool ht_remove(HashTable* table, const void* key);

#ifdef __cplusplus
}
#endif
