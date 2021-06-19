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

#pragma once

#include "bs.h"
#include "bsVec.h"
#include "bsHashMap.h"

class vwReplayAlloc {
 public:
    vwReplayAlloc (void);
    ~vwReplayAlloc(void);
    void reset(void);

    u32  malloc(u32 size);
    void free  (u32 vPtr);


 private:
    int getBinForChunk  (u32 size) const;
    int getBinForRequest(u32 size) const;

    // Some definitions
    enum ChunkState { EMPTY, FREE, USED };

    struct Chunk {
        ChunkState state = EMPTY;
        // Virtual memory infos
        u32 vPtr;
        u32 size;
        int prevCIdx;
        int nextCIdx;
        // Bin infos
        int binNbr;
        int binPrevCIdx;
        int binNextCIdx;
    };

    // Fields
    u32 _highBinSizes[63];
    int _bins[128];
    u32 _wildernessStart = 0;
    int _lastCIdx        = -1;
    bsVec<Chunk> _chunks;
    bsVec<int>   _emptyIndexes;
    bsHashMap<u32,int>_lkupPtrToUsedCIdx;
};
