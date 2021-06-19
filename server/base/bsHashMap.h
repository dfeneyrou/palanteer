// The MIT License (MIT)
//
// Copyright(c) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <string.h>

#include "bs.h"
#include "bsVec.h"

#ifndef PL_GROUP_BSHL
#define PL_GROUP_BSHL 0
#endif

// Simple and fast flat hash table with linear open addressing, dedicated to build a lookup
// - Hashing is internal (for u32 & u64 keys) and an external api is provided (for performance).
//     If external, ensure that it is good enough to avoid clusters, and that external api is always used
// - best storage packing is for 32 bit key size
// - single value per key (overwrite of existing value)

template <typename K, typename V>
class bsHashMap {
public:
    bsHashMap(int initSize=1024) {
        rehash(initSize);
    }
    bsHashMap(const bsHashMap<K,V>& other) noexcept {
        plgAssert(BSHL, other._maxSize>0);
        rehashPo2(other._maxSize);
        _size = other._size; memcpy(&_nodes[0], &other._nodes[0], _size*sizeof(Node)); // Only for numeric or integral types
    }
    bsHashMap(bsHashMap<K,V>&& other) noexcept :
        _nodes(other._nodes), _mask(other._mask), _size(other._size), _maxSize(other._maxSize) {
        other._nodes = 0; other._size = 0; other._maxSize = 0;
    }
    ~bsHashMap(void) {
        delete[] _nodes; _nodes = 0;
    }

    bsHashMap<K,V>& operator=(const bsHashMap<K,V>& other) noexcept {
        rehashPo2(other._maxSize);
        _size = other._size; memcpy(&_nodes[0], &other._nodes[0], _size*sizeof(Node)); // Only for numeric or integral types
        return *this;
    }
    bsHashMap<K,V>& operator=(bsHashMap<K,V>&& other) noexcept {
        delete[] _nodes; _nodes = other._nodes;
        _mask = other._mask; _size = other._size; _maxSize = other._maxSize;
        other._nodes = 0; other._size = 0; other._maxSize = 0; return *this;
    }

    void clear(void) {
        for(u32 i=0; i<_maxSize; ++i) _nodes[i].hash = 0;
        _size = 0;
    }

    bool empty(void) const { return (_size==0); }

    u32  size(void)  const { return _size; }

    u32  capacity(void)  const { return _maxSize; }

    // Exclusive usage, either hash is provided, either hash is computed, do not mix
    bool insert(K key, V value) { return insert(hashFunc(key), key, value); }
    bool insert(u64 hash, K key, V value) {
        plgScope(BSHL, "insert");
        plgData(BSHL, "hash", hash);
        if(hash==0) hash = 1;
        int idx = hash&_mask;
        while(PL_UNLIKELY(_nodes[idx].hash)) {
            plgData(BSHL, "busy index", idx);
            if(_nodes[idx].hash==hash && _nodes[idx].key==key) { // Case overwrite existing value
                plgData(BSHL, "override index", idx);
                _nodes[idx].value = value;
                return false; // Overwritten
            }
            idx = (idx+1)&_mask; // Always stops because load factor < 1
        }
        plgData(BSHL, "write index", idx);
        _nodes[idx] = { hash, key, value }; // Hash is never zero, so "non empty"
        _size += 1;
        if(_size*3>_maxSize*2) {
            rehashPo2(2*_maxSize); // Max load factor is 0.66
        }
        return true; // Added
    }

    // Exclusive usage, either hash is provided, either hash is computed, do not mix
    bool erase(K key) { return erase(hashFunc(key), key); }
    bool erase(u64 hash, K key) {
        plgScope(BSHL, "erase");
        plgData(BSHL, "hash", hash);
        if(hash==0) hash = 1;
        int idx = hash&_mask;
        // Search for the hash
        while(PL_UNLIKELY(_nodes[idx].hash && (_nodes[idx].hash!=hash || _nodes[idx].key!=key))) {
            idx = (idx+1)&_mask; // Always stops because load factor < 1
        }
        if(_nodes[idx].hash==0) {
            plgText(BSHL, "Action", "Not found");
            return false; // Not found
        }
        // Remove it, without using tombstone
        int nextIdx = idx;
        plgData(BSHL, "start index", idx);
        while(1) {
            nextIdx = (nextIdx+1)&_mask;
            u64 nextHash = _nodes[nextIdx].hash;
            plgData(BSHL, "next index", nextIdx);
            if(!nextHash) {
                plgText(BSHL, "Action", "empty next hash: end of cluster");
                break; // End of cluster, we shall erase the previous one
            }
            plgData(BSHL, "next index hash", (int)(nextHash&_mask));
            // Can the 'next hash' replace the one to remove(=idx)? Due to the wrap, it is one of these cases:
            int nextHashIndex = (int)(nextHash&_mask);
 			if( (nextIdx>idx && (nextHashIndex<=idx || nextHashIndex>nextIdx)) || // NextIdx did not wrap
                (nextIdx<idx && (nextHashIndex<=idx && nextHashIndex>nextIdx))) { // NextIdx wrapped
                plgText(BSHL, "Action", "current replaced by next");
                _nodes[idx] = _nodes[nextIdx]; idx = nextIdx;
            }
        }
        plgData(BSHL, "nullified index", idx);
        _nodes[idx].hash = 0; // Empty
        --_size;
        return true;
    }

    // Exclusive usage, either hash is provided, either hash is computed, do not mix
    V* find(K key)           const { return find(hashFunc(key), key); }
    V* find(u64 hash, K key) const {
        plgScope(BSHL,  "find");
        plgData(BSHL, "hash", hash);
        if(hash==0) hash = 1;
        int idx = (int)(hash&_mask);
        while(1) { // Always stops because load factor <= 0.66
            plgData(BSHL, "testing index", idx);
            if(_nodes[idx].hash==hash && _nodes[idx].key==key) {
                plgText(BSHL, "Action", "key found!");
                return &_nodes[idx].value;
            }
            if(_nodes[idx].hash==0) {
                plgText(BSHL, "Action", "empty hash: end of cluster");
                return 0; // Empty node
            }
            idx = (idx+1)&_mask;
        }
        return 0; // Never reached
    }

    void rehash(int newSize) {
        int sizePo2 = 1; while(sizePo2<newSize) sizePo2 *= 2;
        rehashPo2(sizePo2);
    }

    struct Node {
        u64 hash;
        K   key;
        V   value;
    };

    void exportData(bsVec<Node>& nodes) {
        nodes.clear(); nodes.reserve(_size);
        for(u32 i=0; i<_maxSize; ++i) {
            if(_nodes[i].hash!=0) nodes.push_back(_nodes[i]);
        }
    }
    void exportData(bsVec<V>& values) {
        values.clear(); values.reserve(_size);
        for(u32 i=0; i<_maxSize; ++i) {
            if(_nodes[i].hash!=0) values.push_back(_nodes[i].value);
        }
    }

    // Simple hashing based on FNV1a
    static u64 hashFunc(u64 key) { return ((key^14695981039346656037ULL)*1099511628211ULL); }

private:

    // Mandatory: maxSize shall be a power of two
    void rehashPo2(int maxSize) {
        plgScope(BSHL, "rehashPo2");
        plgData(BSHL, "old size", _maxSize);
        plgData(BSHL, "new size", maxSize);
        int   oldSize  = _maxSize;
        Node* oldNodes = _nodes;
        _nodes   = new Node[maxSize];
        for(int i=0; i<maxSize; ++i) _nodes[i].hash = 0;
        _maxSize = maxSize;
        _mask    = maxSize-1;
        _size    = 0;
        for(int i=0; i<oldSize; ++i) { // Transfer the previous filled nodes
            if(oldNodes[i].hash) insert(oldNodes[i].hash, oldNodes[i].key, oldNodes[i].value);
        }
        delete[] oldNodes;
    }

    // Fields
    Node* _nodes   = 0;
    u32   _mask    = 0;
    u32   _size    = 0;
    u32   _maxSize = 0;
};


// Hashing, based on FNV1a-64, but per u64 and not characters
static constexpr u64 BS_FNV_HASH_OFFSET   = 14695981039346656037ULL;
static constexpr u64 BS_FNV_HASH_PRIME    = 1099511628211ULL;
static constexpr u64 BS_FNV_HASH32_OFFSET = 2166136261ULL;
static constexpr u64 BS_FNV_HASH32_PRIME  = 16777619ULL;
inline u64 bsHashStep(u64 novelty, u64 previous=BS_FNV_HASH_OFFSET) { return (novelty^previous)*BS_FNV_HASH_PRIME; }
inline u64 bsHashStepChain(u64 value)  { return bsHashStep(value); } // Recent steps first
template<typename... Args> inline u64 bsHashStepChain(u64 value, Args... args) { return bsHashStep(value, bsHashStepChain(args...)); }
inline u64 bsHashString(const char* s) { u64 h = BS_FNV_HASH_OFFSET; while(*s) h = (h^((u64)(*s++)))*BS_FNV_HASH_PRIME; return h? h:1; }
inline u64 bsHashString(const char* s, const char* sEnd) { u64 h = BS_FNV_HASH_OFFSET; while(s!=sEnd) h = (h^((u64)(*s++)))*BS_FNV_HASH_PRIME; return h? h:1; }
inline u64 bsHash32String(const char* s) { u64 h = BS_FNV_HASH32_OFFSET; while(*s) h = (h^((u64)(*s++)))*BS_FNV_HASH32_PRIME; return h? (h&0xFFFFFFFF):1; }


#ifdef BS_TESTU
// Compile with:
//  g++ -I ../../include -I . -DBS_TESTU=1 -DPL_IMPLEMENTATION=1 -DUSE_PLT=1 -DPL_GROUP_BSHL=1 -ggdb3 -fsanitize=address -fno-omit-frame-pointer -x c++ bsHashMap.h -lasan -lpthread
int
main(int argc, char* argv[])
{
#define TH(v) ((14695981039346656037ULL^(u64)(v))*1099511628211ULL)
    const int ITEM_QTY = 512;
    const int ITERATION_QTY = 50;
    if(argc>1 && !strcmp(argv[1], "console")) peStartModeConsole("testu bsHashMap");
    if(argc>1 && !strcmp(argv[1], "file"   )) peStartModeFile   ("testu bsHashMap", "testuBsHashMap.plt");
    printf("Start unit test for bsHashMap\n");
    auto h = bsHashMap<int, int>(1024); // Start with low capacity to stress the rehash
    int tmp;
    // Add all numbers
    plgBegin(BSHL, "Initial fill");
    for(int i=0; i<ITEM_QTY; ++i) {
        plAssert(h.insert(TH(i), i, i), i, TH(i));
    }
    plgEnd(BSHL, "");
    // Stress through iterations
    for(int iteration=0; iteration<ITERATION_QTY; ++iteration) {
        plgScope (BSHL, "Iteration");
        plgData(BSHL, "Number", iteration);
        {
            plgScope(BSHL, "Check all items are inside");
            plAssert(h.size()==ITEM_QTY);
            for(int i=0; i<ITEM_QTY; ++i) {
                plAssert(h.find(TH(i), i, tmp), iteration, i);
            }
            plgText(BSHL, "Status", "Ok");
            plgData(BSHL, "Capacity", h.capacity());
        }
        const int startI   = iteration*2;
        const int fraction = 2+iteration;
        {
            plgScope(BSHL, "Remove part of items");
            for(int i=startI; i<startI+ITEM_QTY/fraction; ++i) {
                plAssert(h.erase(TH(i), i), iteration, i);
            }
            for(int i=startI; i<startI+ITEM_QTY/fraction; ++i) {
                plAssert(!h.find(TH(i), i, tmp), iteration, i);
            }
            for(int i=startI+1+ITEM_QTY/fraction; i<ITEM_QTY; ++i) {
                plAssert(h.find(TH(i), i, tmp), iteration, i);
            }
            plAssert(h.size()==ITEM_QTY-(ITEM_QTY/fraction));
            plgText(BSHL, "Status", "Ok");
        }
        {
            plgScope(BSHL, "Put back first half of items");
            for(int i=startI; i<startI+ITEM_QTY/fraction; ++i) {
                plAssert(h.insert(TH(i), i, i), iteration, i);
            }
        }
    }
    peStop();
    printf("End unit test for bsHashMap: success\n");
    return 0;
}
#endif
