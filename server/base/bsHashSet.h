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

#include <cstring>

#include "bsVec.h"

// Simple and fast flat hash set with linear open addressing
// - key is a u64 hash (so hashing is external. Ensure good enough hashing to avoid clusters)
// - provided sizes shall always be power of two

class bsHashSet {
public:
    bsHashSet(u32 initSize=1024) {
        rehash(initSize);
    }
    bsHashSet(bsHashSet&& other) noexcept :
        _nodes(other._nodes), _mask(other._mask), _size(other._size), _maxSize(other._maxSize) {
        other._nodes = 0; other._size = 0; other._maxSize = 0;
    }
    ~bsHashSet(void) {
        delete[] _nodes; _nodes = 0;
    }

    bsHashSet& operator=(const bsHashSet& other) noexcept {
        if(&other!=this) {
            rehash(other._maxSize);
            _size = other._size; memcpy(&_nodes[0], &other._nodes[0], _size*sizeof(u64));
        }
        return *this;
    }
    bsHashSet& operator=(bsHashSet&& other) noexcept {
        if(&other!=this) {
            delete[] _nodes; _nodes = other._nodes;
            _mask = other._mask; _size = other._size; _maxSize = other._maxSize;
            other._nodes = 0; other._size = 0; other._maxSize = 0;
        }
        return *this;
    }

    void clear(void) { memset(&_nodes[0], 0, _maxSize*sizeof(u64)); _size = 0; }
    bool empty(void) const { return (_size==0); }
    u32  size(void)  const { return _size; }

    void set(u64 hash) {
        if(hash==0) hash = 1;
        int idx = hash&_mask;
        while(PL_UNLIKELY(_nodes[idx])) {
            if(PL_UNLIKELY(_nodes[idx]==hash)) return; // Already present
            idx = (idx+1)&_mask; // Always stops because load factor (including deleted) < 1
        }
        _nodes[idx]  = hash; // Never zero, so "non empty"
        _size += 1;
        if(_size*3>_maxSize*2) rehash(2*_maxSize); // Max load factor is 0.66
    }

    bool unset(u64 hash) {
        if(hash==0) hash = 1;
        int idx = hash&_mask;
        // Search for the hash
        while(PL_UNLIKELY(_nodes[idx] && _nodes[idx]!=hash)) {
            idx = (idx+1)&_mask; // Always stops because load factor (including deleted) < 1
        }
        if(_nodes[idx]==0) return false; // Not found
        // Remove it, without using tombstone
        int nextIdx = idx;
        while(1) {
            nextIdx = (nextIdx+1)&_mask;
            u64 nextHash = _nodes[nextIdx];
            if(!nextHash) break; // End of cluster, we shall erase the previous one
            if((int)(nextHash&_mask)<=idx || (int)(nextHash&_mask)>nextIdx) { // Check that the 'next hash' can be moved before
                _nodes[idx] = nextHash; idx = nextIdx;
            }
        }
        _nodes[idx] = 0; // Empty
        --_size;
        return true;
    }

    bool find(u64 hash) {
        if(hash==0) hash = 1;
        int idx = hash&_mask;
        while(1) { // Always stops because load factor <= 0.66
            if(_nodes[idx]==hash) return true;
            if(_nodes[idx]==0)    return false; // Empty node
            idx = (idx+1)&_mask;
        }
        return false; // Never reached
    }

    void rehash(int maxSize) {
        int  oldSize  = _maxSize;
        u64* oldNodes = _nodes;
        _nodes   = new u64[maxSize];
        memset(&_nodes[0], 0, maxSize*sizeof(u64));
        _maxSize = maxSize;
        _mask    = maxSize-1;
        _size    = 0;
        for(int i=0; i<oldSize; ++i) { // Transfer the previous filled nodes
            if(oldNodes[i]) set(oldNodes[i]);
        }
        delete[] oldNodes;
    }


private:
    u64* _nodes   = 0;
    u32  _mask    = 0;
    u32  _size    = 0;
    u32  _maxSize = 0;
};
