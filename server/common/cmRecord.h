// Palanteer recording library
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

// System
#include <cstdio>

// Internal
#include "bs.h"
#include "bsVec.h"
#include "bsList.h"
#include "bsString.h"
#include "bsHashMap.h"


// Constants
constexpr static int cmChunkSize     = 256;   // Chunk event quantity for disk storage
constexpr static int cmMRScopeSize   = 8;     // Event pyramid subsampling factor (in memory)
constexpr static int cmElemChunkSize = 32/4*cmChunkSize; // Chunk elem quantity. Elem chunk byte size matches Event chunk byte size (so we can share raw file and cache)
constexpr static int cmMRElemSize    = 16;    // Size of the elem pyramid subsampling (in memory)
constexpr static u32 PL_INVALID      = 0xFFFFFFFF;
constexpr static int PL_MEMORY_SNAPSHOT_EVENT_INTERVAL = 10000; // Smaller value consumes disk space, bigger value increases reactivity time when accessing detailed allocations
constexpr static int PL_RECORD_FORMAT_VERSION = 5;

// Chunk location (=offset and size) in the big event file
typedef u64 chunkLoc_t;

// Record options description
struct cmStreamInfo {
    bsString appName;
    bsString buildName;
    bsString langName;
    u64 tlvs[PL_TLV_QTY];
};

struct cmLogParam {
    int paramType; // See PL_FLAG_TYPE_... up to PL_FLAG_TYPE_DATA_QTY
    union {
        int32_t  vInt;
        uint32_t vU32;
        int64_t  vS64;
        uint64_t vU64;
        float    vFloat;
        double   vDouble;
        uint32_t vStringIdx;
    };
};


class cmRecord {
    static constexpr int FLAG_ADDED_STRING = 0x40000000; // For internal string additions

public:
    cmRecord(FILE* fdChunks, int cacheMBytes);
    ~cmRecord(void);

    // Event (32 bytes)
    struct Evt {
        // Navigation or memory fields
        union {
            struct { // Hierarchical event
                u32 parentLIdx;  // lIdx of the parent, at level-1
                u32 linkLIdx;    // lIdx of the first child at level+1 for scope start, or the next element at same level for other kinds
            };
            struct { // Memory event. The time is stored in the value field vS64
                u32 memLinkIdx;
                u32 allocSizeOrMIdx; // Alloc: Size,  Dealloc: mIdx of the allocation
            };
            struct { // Memory Elem (plottable). The time is stored in the value field vS64
                u64 memElemValue;
            };
            struct { // Core event
                u32 usedCoreQty;
                u32 reserved;
            };
        };

        // Event fields
        u8  threadId;
        u8  flags;
        u16 lineNbr;
        u8  level;
        u8  reserved1;
        u16 reserved2;  // Explicit padding (present anyway due to u64 alignment and constraint on the full struct size as multiple of 8)

        // Name of the event (semantic depends on the event kind)
        u32 nameIdx;

        // Filename, detail memory name, or CPU id
        union {
            u32 filenameIdx;       // Generic event
            u32 memDetailNameIdx;  // Memory event
            u32 coreId;            // Context switch event
        };

        // Event value
        union {
            u64    vU64;  // First for easy initialization
            s64    vS64;
            int    vInt;
            u32    vU32;
            float  vFloat;
            double vDouble;
            u32    vStringIdx;
        };

        // Specific accessors
        u32 getMemCallQty(void) const { return (u32)(vU64>>32);        } // For memory event only
        u32 getMemByteQty(void) const { return (u32)(vU64&0xFFFFFFFF); } // For memory event only
    };

    // Record errors
    static constexpr int MAX_REC_ERROR_QTY = 100;
    enum RecErrorType : u8 { ERROR_MAX_THREAD_QTY_REACHED, ERROR_TOP_LEVEL_REACHED, ERROR_MAX_LEVEL_QTY_REACHED,
                             ERROR_EVENT_OUTSIDE_SCOPE, ERROR_MISMATCH_SCOPE_END, ERROR_REC_TYPE_QTY };
    struct RecError { // 16 bytes
        RecErrorType type;
        u8  threadId;
        u16 lineNbr;
        u32 filenameIdx;
        u32 nameIdx;
        u32 count;
    };

    // Multi-resolution Elem data (8 bytes)
    struct ElemMR {
        u32 speckUs;
        u32 lIdx;
    };
    struct Elem {
        // Path
        u64 hashPath;
        u64 partialHashPath;  // Does not include the thread hash, if "isThreadHashed".
        u64 threadBitmap;
        u32 hashKey;
        u32 prevElemIdx; // (u32)-1 if root
        // Attributes (most of them applicable for scopes)
        int threadId;
        int nestingLevel;
        u32 nameIdx;
        u32 hlNameIdx; // Name to highlight (=nameIdx for scopes and =parent nameIdx for non-scopes)
        int flags;
        int isPartOfHStruct;
        int isThreadHashed;
        double absYMin;
        double absYMax;
        // Multi resolution data
        bsVec<u32>           lastLiveLocChunk;
        bsVec<chunkLoc_t>    chunkLocs;
        bsVec<bsVec<ElemMR>> mrSpeckChunks;
    };

    // Memory snapshot element
    struct MemSnapshot {
        s64 timeNs;
        u64 fileLoc;
        u32 allocMIdx;
    };

    // Log category
    struct LogElem {
        int elemIdx;
        int threadId;
        int logLevel;    // 0=Debug, 1=Info, 2=Warn, 3=Error
        int categoryId;  // Corresponding nameIdx can be retrieved with the field 'logCategories'
    };

#define LOC_STORAGE(name)                      \
    bsVec<Evt>        name##LastLiveEvtChunk;  \
    bsVec<chunkLoc_t> name##ChunkLocs

    // Nesting level
    struct NestingLevel {
        LOC_STORAGE(nonScope);
        LOC_STORAGE(scope);
        bsVec<bsVec<u32>> mrScopeSpeckChunks;  // scope chunks per multi-resolution level
    };

    // Lock
    struct Lock {
        u32        nameIdx;
        bsVec<int> waitingThreadIds;
    };

    struct String {
        bsString value;
        bsString unit;
        u64      hash;
        u64      threadBitmapAsName;
        int      alphabeticalOrder;
        int      lineQty;    // Multi-line management
        int      lockId;     // -1 means not a lock
        int      categoryId; // -1 means not a category
        bool     isExternal;
        bool     isHexa;     // Hexadecimal display desired
    };

    // Thread
    struct Thread {
        u64 threadHash       = 0;
        u64 threadUniqueHash = 0;
        int nameIdx;
        int groupNameIdx = -1;
        int streamId;
        s64 durationNs;
        u32 elemEventQty;
        u32 memEventQty;
        u32 ctxSwitchEventQty;
        u32 lockEventQty;
        u32 logEventQty;
        bsVec<NestingLevel> levels;
        LOC_STORAGE(memAlloc);
        LOC_STORAGE(memDealloc);
        LOC_STORAGE(memPlot);
        LOC_STORAGE(ctxSwitch);
        LOC_STORAGE(softIrq);
        LOC_STORAGE(lockWait);
        bsVec<u32>         memDeallocMIdx; // Per alloc mIdx;
        bsVec<MemSnapshot> memSnapshotIndexes;
    };

    // Chunk location in the big file: 36 bit for the offset, and 28 bits for the size of the chunk
    // For 2 billions events (max), offset is < 24*2e9.
    // Chunk size is <= 24*cmChunkSize (=6144 bytes). Remains the memory snapshot, which can then go up to 16 MB
    static chunkLoc_t makeChunkLoc  (u64 offset, u64 size) { return (size<<36) | offset; }
    static u64        getChunkOffset(chunkLoc_t pos)       { return pos&0xFFFFFFFFFLL; }
    static int        getChunkSize  (chunkLoc_t pos)       { return (int)(pos>>36); }

    // Accessors and updaters
    const bsVec<Evt>& getEventChunk(chunkLoc_t pos, const bsVec<cmRecord::Evt>* lastLiveChunk=0) const; // Buffer is valid at least up to the next call
    const bsVec<u32>& getElemChunk (chunkLoc_t pos, const bsVec<u32>* lastLiveChunk=0) const; // Buffer is valid at least up to the next call
    void getMemorySnapshot(int threadId, int snapshotIdx, bsVec<u32>& currentAllocMIdxs) const;

    // Strings update and access
    const String& getString(u32 idx) const { return (idx&FLAG_ADDED_STRING)? _addedStrings[idx&(~FLAG_ADDED_STRING)] : _strings[idx]; }
    bsVec<String>& getStrings(void) { return _strings; }
    void loadExternalStrings(void);
    void updateString(int strIdx);
    void updateThreadString(int tId);
    void sortStrings(void);

    // Delta records (for thread-safe live display of recording)
    struct DeltaString {
        int stringId;
        u64 threadBitmapAsName;
        int lockId;
        int categoryId;
    };
    struct Delta {
        // Stats
        s64 durationNs;
        u64 recordByteQty;
        int coreQty;
        u32 elemEventQty;
        u32 memEventQty;
        u32 ctxSwitchEventQty;
        u32 lockEventQty;
        u32 logEventQty;
        u32 errorQty;
        // Delta buffers
        LOC_STORAGE(coreUsage);
        LOC_STORAGE(log);
        LOC_STORAGE(lockNtf);
        LOC_STORAGE(lockUse);
        bsVec<cmStreamInfo> streams;  // Full list of stream infos
        bsVec<Lock>   locks;   // Full lock structure
        bsVec<Thread> threads; // Full list of threads but with only delta buffers
        bsVec<Elem>   elems;   // Full list of elems   but with only delta buffers
        bsVec<int>    logCategories; // Full list
        bsVec<String> strings; // With recomputation of alphabetical order
        bsVec<DeltaString> updatedStrings; // Only the delta
        bsVec<int>    updatedThreadIds;
        bsVec<u32>    updatedElemIds;
        bsVec<u32>    updatedLockIds;
        RecError      errors[MAX_REC_ERROR_QTY]; // Delta array
        // Methods
        void reset(void);
    };

    // Live update
    bool updateFromDelta(Delta* delta);

    // Others
    void buildLogCategories(void);

    // Fields
    bsString appName;
    bsString recordPath;
    bsDate   recordDate;
    int      compressionMode;
    int      isMultiStream;
    s64      durationNs = 0;
    u64      recordByteQty   = 0;
    int      coreQty    = 0;
    u32      elemEventQty = 0;
    u32      memEventQty  = 0;
    u32      ctxSwitchEventQty  = 0;
    u32      lockEventQty  = 0;
    u32      logEventQty = 0;
    u32      errorQty = 0;
    LOC_STORAGE(coreUsage);
    LOC_STORAGE(log);
    LOC_STORAGE(lockNtf);
    LOC_STORAGE(lockUse);
    bsVec<cmStreamInfo> streams;
    bsVec<Lock>        locks;
    bsVec<Thread>      threads;
    bsVec<Elem>        elems;
    bsVec<int>         logCategories;
    bsVec<LogElem>     logElems;
    bsHashMap<int,int> elemPathToId;
    RecError errors[MAX_REC_ERROR_QTY];

private:

    // Strings (external lkup, loaded, and added ones
    bsHashMap<u64, int> _extStringsHashToStrIdx;
    bsVec<bsString>     _extStrings;
    bsVec<String>       _strings;
    bsVec<String>       _addedStrings;
    bsVec<u64>          _workThreadUniqueHash; // Used only at record building time

    // Cache
    FILE* _fdChunks;
    int   _cacheMaxEntries;
    u8*   _fileChunkBuffer;
    struct CacheEntry {
        u64        chunkOffset;
        bool       isEvent;
        bsVec<Evt> chunkEvent;
        bsVec<u32> chunkElem;
    };
    typedef bsList<CacheEntry>::const_iterator LRUIterator;
    mutable bsList<CacheEntry>          _cacheLRU;
    mutable bsHashMap<u64, LRUIterator> _cacheAccess;
    mutable bsVec<u8> _workingBuffer; // For compression
};


cmRecord* cmLoadRecord(const bsString& path, int cacheMBytes, bsString& errorMsg);
