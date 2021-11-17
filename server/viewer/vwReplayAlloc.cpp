// Palanteer viewer
// Copyright (C) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// This file implements a virtual memory allocator in order to nicely represent the program memory allocations
// See http://gee.cs.oswego.edu/dl/html/malloc.html

// System
#include <math.h>

// Internal
#include "vwReplayAlloc.h"
#include "vwConst.h"


#ifndef PL_GROUP_RALLOC
#define PL_GROUP_RALLOC 0
#endif

#define SIZE_MIN 1

vwReplayAlloc::vwReplayAlloc(void) :
    _lkupPtrToUsedCIdx(16)
{
    // Some reservations
    _chunks.reserve(1024);
    _emptyIndexes.reserve(1024);

#if 0
    // Computation of exponentially increasing bin sizes (formula: binSizeN+1-binSizeN = 8*p^N
    // Computed once for all. Approximative value is ok.
    // Kept in the code both for explanatory reason of the below code, and in case this below code needs to evolve
    double s = (2147483648.-512)/8.;
    double step = 0.0001;
    double p = 1. + step;
    while(1) {
        double v = (pow(p, 64)-1.)/(p-1.);
        if(v>s) { printf("Best incrFactor is %f => %f\n", p-step, v); printf("LARGER THAN %f\n", s); break; }
        p += step;
    }
#endif

    // Some init
    double incrFactor = 1.331;
    double value      = 512;
    double incr       = 8;
    for(int i=0; i<63; ++i) {
        incr  *= incrFactor;
        value += incr;
        _highBinSizes[i] = (u32)value;
    }
    reset();
}


vwReplayAlloc::~vwReplayAlloc(void)
{
}


void
vwReplayAlloc::reset(void)
{
    _chunks           .clear();
    _emptyIndexes     .clear();
    _lkupPtrToUsedCIdx.clear();
    _wildernessStart = 0;
    _lastCIdx        = -1;
    memset(&_bins[0], 0xFF, sizeof(_bins)); // -1 everwhere as a default
}


int
vwReplayAlloc::getBinForChunk(u32 size) const // [Floor case] Bin limit shall be just below chunk size. Ex: size=31 => bin 3
{
    // Regular 8 bytes bins up to 512
    if(size<=512) return size/8; // -> bin number is in [0; 64]

    // Exponentially increasing size after 512. Bin number in [65; 127]
    int binNbr=0;
    while(binNbr<62 && _highBinSizes[binNbr]<=size) ++binNbr;
    return 65+binNbr-1;
}


int
vwReplayAlloc::getBinForRequest(u32 size) const // [Ceil case] Bin limit shall be above or equal to request size. Ex: size=31 => bin 4
{
    // Regular 8 bytes bins up to 512
    if(size<=512) return (size+7)/8; // -> bin number is in [0; 64]

    // Exponentially increasing size after 512. Bin number in [65; 127]
    int binNbr=0;
    while(binNbr<62 && _highBinSizes[binNbr]<size) ++binNbr;
    return 65+binNbr;
}


u32
vwReplayAlloc::malloc(u32 size)
{
    plgScope(RALLOC,  "ReplayAlloc::malloc");
    plgVar(RALLOC, size);

    // Look for best fit, starting from first compatible bin
    if(size<SIZE_MIN) size = SIZE_MIN;
    int binNbr = getBinForRequest(size);
    while(binNbr<128 && _bins[binNbr]<0) ++binNbr;
    plgVar(RALLOC, binNbr);

    // If no suitable bin found, allocate in the wilderness
    if(binNbr==128) {
        // Get an empty chunk
        if(_emptyIndexes.empty()) { _emptyIndexes.push_back(_chunks.size()); _chunks.push_back( { EMPTY } ); }
        int nIdx = _emptyIndexes.back(); _emptyIndexes.pop_back();
        Chunk& n = _chunks[nIdx];
        plAssert(n.state==EMPTY);

        // Turn it into a USED chunk
        n = { USED, _wildernessStart, size, _lastCIdx, -1, getBinForChunk(size), -1, -1 };
        if(_lastCIdx>=0) _chunks[_lastCIdx].nextCIdx = nIdx;
        _wildernessStart += size;
        _lastCIdx         = nIdx;
        _lkupPtrToUsedCIdx.insert(n.vPtr, nIdx);

        // Return it
        plgText(RALLOC, "kind", "Wild chunk");
        plgData(RALLOC, "new cidx", nIdx);
        plgData(RALLOC, "new vPtr", n.vPtr);
        plgData(RALLOC, "prev cidx", n.prevCIdx);
        return n.vPtr;
    }

    // Get the free chunk from the bin
    plgAssert(RALLOC, binNbr<128);
    int cIdx = _bins[binNbr];
    Chunk& cf = _chunks[cIdx];
    plgBegin(RALLOC, "Found existing free chunk");
    plgVar(RALLOC, cIdx, cf.vPtr, cf.size);
    plgEnd(RALLOC, "");
    plAssert(cf.state==FREE, (int)cf.state, binNbr, cIdx, cf.size, cf.binPrevCIdx, cf.binNextCIdx);
    plAssert(cf.size>=size, cf.size, size, binNbr);

    // Split it if some free size is left after allocation
    if(cf.size>size) {
        plgScope(RALLOC, "Add free chunk after");
        // Create the remaining free chunk
        if(_emptyIndexes.empty()) { _emptyIndexes.push_back(_chunks.size()); _chunks.push_back( { EMPTY } ); } // Invalidates cf
        int nIdx = _emptyIndexes.back(); _emptyIndexes.pop_back();
        Chunk& c = _chunks[cIdx];

        // Install the chunk as the new free one
        Chunk& n = _chunks[nIdx];
        plAssert(n.state==EMPTY);
        int newBinNbr = getBinForChunk(c.size-size);
        n = { FREE, c.vPtr+size, c.size-size, cIdx, c.nextCIdx, newBinNbr, -1, -1 };
        plgData(RALLOC, "free cidx", nIdx);
        plgData(RALLOC, "free vPtr", n.vPtr);
        plgData(RALLOC, "free size", n.size);
        if(n.nextCIdx>=0) _chunks[n.nextCIdx].prevCIdx = nIdx;
        if(cIdx==_lastCIdx) _lastCIdx = nIdx;

        // Update bins: insert at bin head
        n.binNextCIdx = _bins[newBinNbr];
        if(n.binNextCIdx>=0) _chunks[n.binNextCIdx].binPrevCIdx = nIdx;
        _bins[newBinNbr] = nIdx;
        plgData(RALLOC, "inserted at head of bin", newBinNbr);

        // Update the used chunk
        c.nextCIdx = nIdx;
        c.size     = size;
        plAssert(c.vPtr+c.size==n.vPtr, c.vPtr, c.size, n.vPtr);
    }

    // Update bins: fully remove from the chain
    Chunk& c = _chunks[cIdx];  // cf may have been invalidated
    if(c.binPrevCIdx<0)  _bins[c.binNbr] = c.binNextCIdx;
    else                 _chunks[c.binPrevCIdx].binNextCIdx = c.binNextCIdx;
    if(c.binNextCIdx>=0) _chunks[c.binNextCIdx].binPrevCIdx = c.binPrevCIdx;

    // Update the chunk
    c.state  = USED;
    c.binNbr = getBinForChunk(c.size);
    plgData(RALLOC, "new  bin",  c.binNbr);
    plgData(RALLOC, "new  size", c.size);
    plgData(RALLOC, "prev cIdx", c.prevCIdx);
    plgData(RALLOC, "next cIdx", c.nextCIdx);
    _lkupPtrToUsedCIdx.insert(c.vPtr, cIdx);
    return c.vPtr;
}


void
vwReplayAlloc::free(u32 vPtr)
{
    plgScope(RALLOC,  "ReplayAlloc::free");
    plgVar(RALLOC, vPtr);

    // Get the chunk and update it
    int* cIdxPtr = _lkupPtrToUsedCIdx.find(vPtr);
    plAssert(cIdxPtr, vPtr);
    int cIdx = *cIdxPtr;
    _lkupPtrToUsedCIdx.erase(vPtr);
    plgVar(RALLOC, cIdx);

    Chunk& c  = _chunks[cIdx];
    plAssert(c.state==USED);
    plgData(RALLOC, "free size", c.size);
    plgData(RALLOC, "prev cIdx", c.prevCIdx);
    plgData(RALLOC, "next cIdx", c.nextCIdx);
    c.state = FREE;

    // Merge with previous chunk, if free
    if(c.prevCIdx>=0 && _chunks[c.prevCIdx].state==FREE) {
        plgScope(RALLOC, "Merge with previous chunk");
        Chunk& cp = _chunks[c.prevCIdx];
        plAssert(cp.nextCIdx==cIdx, cp.nextCIdx, cIdx);
        plAssert(c.vPtr==cp.vPtr+cp.size, c.vPtr, cp.vPtr, cp.size);

        // Delete previous chunk
        cp.state = EMPTY;
        _emptyIndexes.push_back(c.prevCIdx);
        plgData(RALLOC, "now empty cIdx", c.prevCIdx);

        // Absorb the free chunk before
        plAssert(c.vPtr==cp.vPtr+cp.size);
        c.vPtr  = cp.vPtr;
        c.size += cp.size;
        if(cp.prevCIdx>=0) _chunks[cp.prevCIdx].nextCIdx = cIdx;
        c.prevCIdx = cp.prevCIdx;
        plgData(RALLOC, "new prev cIdx", c.prevCIdx);
        plgData(RALLOC, "new size", c.size);

        // Update bins: fully remove from the chain
        plgData(RALLOC, "old prev prev bin", cp.binPrevCIdx);
        plgData(RALLOC, "old prev next bin", cp.binNextCIdx);
        plgData(RALLOC, "old bin head", _bins[cp.binNbr]);
        if(cp.binPrevCIdx<0)  _bins[cp.binNbr] = cp.binNextCIdx;
        else                  _chunks[cp.binPrevCIdx].binNextCIdx = cp.binNextCIdx;
        if(cp.binNextCIdx>=0) _chunks[cp.binNextCIdx].binPrevCIdx = cp.binPrevCIdx;
        cp.binPrevCIdx = cp.binNextCIdx = -1;
        plgData(RALLOC, "new bin head", _bins[cp.binNbr]);
    }

    // Merge with next chunk, if free
    if(c.nextCIdx>=0 && _chunks[c.nextCIdx].state==FREE) {
        plgScope(RALLOC, "Merge with next chunk");
        Chunk& cn = _chunks[c.nextCIdx];
        plAssert(cn.prevCIdx==cIdx, cn.prevCIdx, cIdx);
        plAssert(cn.vPtr==c.vPtr+c.size);

        // Delete next chunk
        if(c.nextCIdx==_lastCIdx) _lastCIdx = cIdx;
        cn.state = EMPTY;
        _emptyIndexes.push_back(c.nextCIdx);
        plgData(RALLOC, "now empty cIdx", c.nextCIdx);

        // Absorb the free chunk after
        c.size += cn.size;
        if(cn.nextCIdx>=0) _chunks[cn.nextCIdx].prevCIdx = cIdx;
        c.nextCIdx = cn.nextCIdx;
        plgData(RALLOC, "new next cIdx", c.nextCIdx);
        plgData(RALLOC, "new size",      c.size);

        // Update bins: fully remove from the chain
        plgData(RALLOC, "old next prev bin", cn.binPrevCIdx);
        plgData(RALLOC, "old next next bin", cn.binNextCIdx);
        plgData(RALLOC, "old bin head", _bins[cn.binNbr]);
        if(cn.binPrevCIdx>=0)  _chunks[cn.binPrevCIdx].binNextCIdx = cn.binNextCIdx;
        else _bins[cn.binNbr] = cn.binNextCIdx;
        if(cn.binNextCIdx>=0) _chunks[cn.binNextCIdx].binPrevCIdx = cn.binPrevCIdx;
        cn.binPrevCIdx = cn.binNextCIdx = -1;
        plgData(RALLOC, "new bin head", _bins[cn.binNbr]);
    }

    //  Update bins: insert at bin head
    c.binNbr        = getBinForChunk(c.size);
    c.binPrevCIdx   = -1;
    c.binNextCIdx   = _bins[c.binNbr];
    if(c.binNextCIdx>=0) _chunks[c.binNextCIdx].binPrevCIdx = cIdx;
    _bins[c.binNbr] = cIdx;
    plgData(RALLOC, "inserted at head of bin", c.binNbr);
}
