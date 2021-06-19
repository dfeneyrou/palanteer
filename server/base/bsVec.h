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

// Simple, partial but functional replacement for vector<>. Without bloat, so usable in debug mode
// Important: works only for types without constructors (did not bother with the SFINAE thing to handle both, as not needed in Palanteer)

// Warning: clearing or "shrink-resizing" does not destruct objects (optimization) so you have to reinitialize them properly if you "grow-resize"

// System
#include <initializer_list>
#include <utility>     // swap, forward
#include <cstring>     // memcmp

// Internal
#include "palanteer.h" // Assertion, logging


// Debug flag to "shake" a bit the re-allocation. Lower performances of course, but it helps to detect cases of "free after use"
#define BS_IGNORE_RESERVE 0

#ifndef PL_GROUP_BSVEC
#define PL_GROUP_BSVEC 1
#endif

template <typename T>
class bsVec
{
public:
    typedef       T value_type; // Required for some std functions
    typedef       T* iterator;
    typedef const T* const_iterator;

    // Constructors, destructors, assignement
    bsVec(int startSize=0) :
        _size(startSize), _capacity(startSize), _array(startSize>0? new T[startSize]:0) {
        plgAssert(BSVEC, startSize>=0);
    }
    bsVec(bsVec<T>&& other) noexcept :
        _size(other._size), _capacity(other._capacity), _array(other._array) {
        other._array = 0;  other._size = 0; other._capacity = 0;
    }
    bsVec(bsVec<T> const& other) :
        _size(other._size), _capacity(other._capacity), _array(other._capacity>0? new T[other._capacity]:0) {
        plgAssert(BSVEC, _capacity>=_size);
        for(int i=0; i<other._size; ++i) _array[i] = other._array[i];
    }
    bsVec(const_iterator begin, const_iterator end) :
        _size(0), _capacity((int)(end-begin)), _array(end!=begin? new T[end-begin]:0) {
        plgAssert(BSVEC, _capacity>=0);
        for(const T* p=begin; p!=end; ++p) _array[_size++] = *p;
    }
    bsVec(std::initializer_list<T> lst) :
        _size(0), _capacity((int)lst.size()), _array(lst.size()? new T[lst.size()]:0) {
        for(auto& e:lst) _array[_size++] = e;
    }
    virtual ~bsVec(void) {
        _size = 0 ; _capacity = 0 ; delete[] _array; _array = 0;
    }

    // Assignement
    bsVec<T>& operator=(bsVec<T> other) noexcept { swap(other); return *this; } // Trick: no const&, but force copy. Equivalent, but simpler
    void swap(bsVec<T>& other) noexcept {
        int tmp = other._size; other._size = _size; _size = tmp;
        tmp = other._capacity; other._capacity = _capacity; _capacity = tmp;
        T* tmpA = other._array; other._array = _array; _array = tmpA;
    }
    friend void swap(bsVec<T>& a, bsVec<T>& b) noexcept { a.swap(b); }

    // Size & capacity management
    void clear(void) noexcept { _size = 0; }
    void resize(int newSize) { if(newSize>_capacity) reserve_(newSize*2+1); _size = newSize; }
    void reserve(int newCapacity) {
        if(BS_IGNORE_RESERVE) return; // May help finding some bug (in association with ASAN)
        reserve_(newCapacity);
    }

    // Container accessors
    bool empty(void)    const { return (_size==0); }
    int  size(void)     const { return _size; }
    int  capacity(void) const { return _capacity; }
    bool operator==(const bsVec<T>& other) const { return _size==other._size && !memcmp(_array, other._array, _size*sizeof(T)); }
    bool operator!=(const bsVec<T>& other) const { return !operator==(other); }

    // Operations on elements
    T&       operator[](int index)       { plgAssert(BSVEC, index>=0 && index<_size, index, _size); return _array[index]; }
    const T& operator[](int index) const { plgAssert(BSVEC, index>=0 && index<_size, index, _size); return _array[index]; }

    void push_back(const T& e) { emplace_back(e); }
    void push_back(T&& e)      { emplace_back(std::forward<T>(e)); }
    template<class... Args > void emplace_back(Args&&... args) {
        if(_size==_capacity) reserve_(2*_capacity+1);
        _array[_size++] = T(std::forward<Args>(args)...);
    }
    void     pop_back(void) { --_size; }
    T&       front(void)       { plgAssert(BSVEC, _size); return _array[0]; }
    const T& front(void) const { plgAssert(BSVEC, _size); return _array[0]; }
    T&       back(void)        { plgAssert(BSVEC, _size); return _array[_size-1]; }
    const T& back(void)  const { plgAssert(BSVEC, _size); return _array[_size-1]; }
    void     insert(const T* it, const T& e) {
        int idx = it-_array;
        plgAssert(BSVEC, idx>=0 && idx<=_size, idx, _size);
        if(_size==_capacity) reserve_(2*_capacity+1);
        T* src = _array+_size-1; T* dst = _array+_size;
        while(src>=_array+idx) *dst-- = std::move(*src--);
        _array[idx] = e; ++_size;
    }
    void erase(const T* itBegin, const T* itEnd) {
        plgAssert(BSVEC, itBegin>=_array && itBegin<=_array+_size);
        plgAssert(BSVEC, itEnd  >=_array && itEnd  <=_array+_size);
        plgAssert(BSVEC, itBegin<=itEnd);
        T* src = (T*)itEnd; T* dst = (T*)itBegin;
        while(src!=_array+_size) *dst++ = std::move(*src++);
        _size -= itEnd-itBegin;
    }
    void erase(const T* it) { erase(it, it+1); }

    // Iterators
    iterator       begin(void)       { return _array+0; }
    const_iterator begin(void) const { return _array+0; }
    iterator       end(void)         { return _array+_size; }
    const_iterator end(void)   const { return _array+_size; }

private:
    void reserve_(int newCapacity) {
        if(newCapacity<=_capacity) return;
        T* oldArray = _array;
        _capacity   = newCapacity;
        _array      = new T[_capacity];
        for(int i=0; i<_size; ++i) _array[i] = std::move(oldArray[i]);
        delete[] oldArray;
    }
    int _size;
    int _capacity;
protected:
    T*  _array;
};
