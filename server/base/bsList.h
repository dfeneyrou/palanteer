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

// Simple list (partial implementation)
// Note: iterators have not been tested thoroughfully (only the used part...)

// Internal
#include "palanteer.h"


template <typename T>
class bsList
{

private:
    struct Node {
        Node* prev, *next;
        T     value;
    };

public:
    typedef T value_type; // Required for some std functions
    class iterator;
    class const_iterator;

    bsList(void) = default;
    ~bsList(void) { clear(); }

    bool empty(void) const { return (_size==0); }
    int  size(void)  const { return _size; }

    iterator insert(const_iterator pos, const T& value) {
        Node* posNode = pos._node;
        Node* newNode = new Node({ posNode->prev, posNode, value });
        if(posNode->prev) posNode->prev->next = newNode;
        if(posNode==_first) _first = newNode;
        posNode->prev = newNode;
        ++_size;
        return newNode;
    }

    iterator erase(const_iterator pos) {
        plAssert(_size);
        Node* posNode = pos._node;
        plAssert(posNode!=&_end);
        posNode->next->prev = posNode->prev;
        if(posNode->prev) posNode->prev->next = posNode->next;
        if(posNode==_first) _first = posNode->next;
        Node* nextNode = posNode->next;
        delete posNode;
        --_size;
        return nextNode;
    }

    void pop_front(void) { erase(_first); }

    void pop_back(void) { erase(_end.prev); }

    void push_front(const T& value) {
        Node* newNode = new Node({ 0, _first, value });
        _first->prev = newNode;
        _first = newNode;
        ++_size;
    }

    void push_back(const T& value) {
        Node* newNode = new Node({ _end.prev, &_end, value });
        if(_end.prev) _end.prev->next = newNode;
        if(&_end==_first) _first = newNode;
        _end.prev = newNode;
        ++_size;
    }

    void clear(void) { while(!empty()) pop_front(); }

    void splice(const_iterator pos, bsList& other, const_iterator otherIt) {
        // Detach otherNode from the other list
        Node* otherNode = otherIt._node;
        Node* posNode   = pos._node;
        plAssert(posNode!=&_end);
        plAssert(otherNode!=&_end);
        if(otherNode==posNode) return; // Nothing to do
        otherNode->next->prev = otherNode->prev;
        if(otherNode->prev) otherNode->prev->next = otherNode->next;
        if(otherNode==other._first) other._first = otherNode->next;
        --other._size;
        // Insert otherNode in current list
        if(posNode->prev) posNode->prev->next = otherNode;
        otherNode->prev = posNode->prev;
        posNode->prev   = otherNode;
        otherNode->next = posNode;
        if(posNode==_first) _first = otherNode;
        ++_size;
    }

    T&       front(void)       { plAssert(_size); return _first->value; }
    const T& front(void) const { plAssert(_size); return _first->value; }
    T&       back(void)        { plAssert(_size); return _end.prev->value; }
    const T& back(void)  const { plAssert(_size); return _end.prev->value; }

    // Iterators
    class iterator {
    public:
        iterator(typename bsList<T>::Node* node) : _node(node) { }
        iterator (void) = default;
        ~iterator(void) = default;
        T&       operator*(void)        { return _node->value; }
        const T& operator*(void)  const { return _node->value; }
        T*       operator->(void) const { return &_node->value; }
        T&       operator++(void)       { _node = _node->next; return *this; }
        T        operator++(int)        { iterator newIt(*this); _node = _node->next; return newIt; }
    private:
        bsList<T>::Node* _node = 0;
        friend bsList<T>;
    };

    class const_iterator {
    public:
        const_iterator(typename bsList<T>::Node* node) : _node(node) { }
        const_iterator (void) = default;
        ~const_iterator(void) = default;
        const T& operator*(void)  const { return _node->value; }
        const T* operator->(void) const { return &_node->value; }
        T&       operator++(void)       { _node = _node->next; return *this; }
        T        operator++(int)        { iterator newIt(*this); _node = _node->next; return newIt; }
    private:
        bsList<T>::Node* _node = 0;
        friend bsList<T>;
    };

    iterator       begin (void)       { return _first; }
    const_iterator cbegin(void) const { return _first; }
    iterator       end   (void)       { return &_end; }
    const_iterator cend  (void) const { return &_end; }

private:
    int   _size  = 0;
    Node  _end   = { 0, 0 };
    Node* _first = &_end;
};
