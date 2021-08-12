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

#include "bs.h"
#include "bsVec.h"

class bsStringUtf16;

// Unix way of coding unicode
//  Pro : compatibility with ascii and all existing string API
//  Cons: variable size. Also needs 3 bytes to do the equivalent of 2 bytes of UTF16 (so for all characters except latin-1...)
// Note : Implementation limited to 3 bytes which is the range of the Basic Multilingual Plane (BMP)
class bsString : public bsVec<u8>
{
public:
    // Contructors
    bsString(void) : bsVec<u8>() { }
    bsString(const bsString& s) : bsVec<u8>(s) { };
    bsString(const char* s);
    bsString(const char* begin, const char* end) : bsVec<u8>((u8*)begin, (u8*)end) { };
    bsString& operator=(bsString other) noexcept { swap(other); return *this; } // Trick: no const&, but force copy. Equivalent, but simpler

    // Operators +
    bsString  operator+(const char* s) const;
    bsString  operator+(const bsString& s) const;
    bsString& operator+=(const char* s);
    bsString& operator+=(const bsString& s);

    // Helpers
    const char* toChar(void) const {
        if(capacity()==size() || _array[size()]!=0) {  // Ensure zero termination
            ((bsString*)this)->push_back(0); ((bsString*)this)->pop_back();
        }
        return (const char*)_array;
    }
    bool startsWith(const bsString& prefix) const;
    bool endsWith  (const bsString& suffix, int suffixStartIdx=0) const;
    int  findChar (char c) const;
    int  rfindChar(char c) const;
    bsString subString(int startIdx, int endIdx=-1) const;
    bsString capitalize(void) const;
    bsString& strip(void);
    bsStringUtf16 toUtf16(void) const;
};


// Windows way of coding unicode
//  Cons: incompatibility with ascii and all existing interfaces (cf Windows duplicated API A&W...). Subject to endianess. Also twice bigger size than UTF8 for ascii
//  Pro : (almost) always 2 bytes for (almost) all characters in the world. More efficient than UTF8 for BMP characters not in latin-1
// Note : Implementation limited to 16 bits (no support for 32 bits codepoints) which is the range of the BMP.
//       This approximation allows: a fixed size (16 bit) codepoint + direct mapping between the unicode codepoint and the (limited) UTF16 value
class bsStringUtf16 : public bsVec<char16_t>
{
public:
    bsStringUtf16(void) : bsVec<char16_t>() { }
    bsStringUtf16(const bsStringUtf16& s) : bsVec<char16_t>(s) { }
    bsStringUtf16(const char16_t* s);

    bsStringUtf16 operator+(const char* s) const;
    bsStringUtf16 operator+(const bsStringUtf16& s) const;
    bsStringUtf16& operator+=(const char* s);
    bsStringUtf16& operator+=(const bsStringUtf16& s);

    const char16_t* toChar16(void) const {
        ((bsStringUtf16*)this)->push_back(0); ((bsStringUtf16*)this)->pop_back();
        return (const char16_t*)_array;
    }
    bsString toUtf8(void) const;
};


// Convert one UTF8 char into unicode. It returns a pointer to the next UTF8 char, and null in case of error
u8* bsCharUtf8ToUnicode(u8* begin, u8* end, char16_t& codepoint);

// Convert one codepoint into UTF. The result is appended at the end of the string
bool bsCharUnicodeToUtf8(char16_t codepoint, bsString& outUtf);

inline
bool
bsIsUnicodeDisplayable(char16_t codepoint)
{
    return codepoint>=0x20 && codepoint<0xFFF0 && (codepoint<0x7F || codepoint>0xA0);
}
