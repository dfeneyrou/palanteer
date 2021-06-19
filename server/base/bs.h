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

// System
#include <cstdint>

// Internal
#include "palanteer.h"

// Shorter base types
typedef uint8_t   u8;
typedef int8_t    s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;

// Useful types
typedef s64 bsUs_t;
typedef u32 bsColor_t;   // 0xAABBGGRR, directly convertible in GL color

// Constants
#define COLOR_TRANSPARENT 0x00000000
#define COLOR_BLACK       0xFF000000
#define COLOR_WHITE       0xFFFFFFFF
#define COLOR_SHADOW      0x80000000 // Half transparent black

// Utils
template <class T> T    bsAbs(T a) { return (a>=0.)? a:-a; }
template <class T> T    bsRound(T a) { return (a>=0.)? (a+0.5):-(0.5-a); }
template <class T1, class T2> T1 bsMax(T1 a, T2 b) { return a>b? a:b; }
template <class T1, class T2> T1 bsMin(T1 a, T2 b) { return a<b? a:b; }
template <class T> T    bsMinMax(T v, T min, T max) { return v<=min? min: ((v>=max)?max:v); }
template <class T> int  bsSign(T a)     { return a>=0? 1:-1; }
template <class T> void bsSwap(T& a, T& b) { T tmp = b; b = a; a = tmp; }
inline int bsDivCeil(int num, int denum) { return (num+denum-1)/denum; }

// 2D position
struct bsVI2 {
    int x, y;
};

struct bsV2 {
    float x, y;
};

// Rectangle / bounding box
struct bsRect {
    float x0=0., y0=0., x1=0., y1=0.;
    bool empty(void) const { return x0>=x1 || y0>=y1; }
};

 // Data for 2D shader
struct bsDraw2d {
    float x, y;      // Screen pos
    float u, v;      // Texture coord (if used)
    bsColor_t color; // Plain color
    float alpha;     // Alpha multiplier
    int   mode;      // 0=plain color 1=texture0-Alpha 2=texture1-RGB
    int   reserved;  // Alignment...
};

// Character with style
struct bsChar {
    char16_t codepoint;
    u16      style; // 4 fg + 4 bg + 3 delta font size (+/-4) + 5 reserved
    // Accessors
    static inline u16 getStyle(u32 fColorIdx, u32 bColorIdx, int deltaSize) {
        return ((fColorIdx&0xF)<<0) | ((bColorIdx&0xF)<<4) | (bsMinMax(deltaSize+3, 0, 7)<<8);
    }
    static inline u16 getFColorIdx(u16 style) { return style&0xF; }
    static inline u16 getBColorIdx(u16 style) { return (style>>4)&0xF; }
    static inline int getDeltaSize(u16 style) { return (int)((style>>8)&0x7)-3; }
    static inline u32 getDeltaSizeCode(u16 style) { return (style>>8)&0x7; }
};

// Date structure
struct bsDate {
    int year=0, month=0, day=0, hour=0, minute=0, second=0; // As displayed (no offset)
    bool isOlderThan(const bsDate& o) const {
        if(year<o.year)      { return true; } if(year>o.year)     { return false; }
        if(month<o.month)    { return true; } if(month>o.month)   { return false; }
        if(day<o.day)        { return true; } if(day>o.day)       { return false; }
        if(hour<o.hour)      { return true; } if(hour>o.hour)     { return false; }
        if(minute<o.minute)  { return true; } if(minute>o.minute) { return false; }
        if(second<o.second)  { return true; } else return false;
    }
    bool isEmpty(void) const { return (year==0); }
};
