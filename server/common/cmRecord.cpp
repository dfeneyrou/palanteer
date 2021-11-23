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

// This file implements the record loading, chunk access and some maintenance helpers

// System
#include <algorithm>
#include <cinttypes>

// Internal
#include "bsOs.h"
#include "cmRecord.h"
#include "cmCompress.h"

// Logging groups
#ifndef PL_GROUP_ITCACHE
#define PL_GROUP_ITCACHE 0
#endif


cmRecord::cmRecord(FILE* fdChunks, int cacheMBytes) :
    _fdChunks(fdChunks), _cacheMaxEntries(bsMin(cacheMBytes, 2000)*1000000/(cmChunkSize*sizeof(cmRecord::Evt)))
{
    plAssert(_fdChunks);

    // Dimension for the cache entry quantity (the hashtable max load factor is 0.66)
    _cacheAccess.rehash((int)((float)_cacheMaxEntries/0.65));
    _fileChunkBuffer = new u8[cmChunkSize*sizeof(Evt)];
    _workingBuffer.reserve(16384); // Will be resized anyway
    _extStrings.reserve(1024);
    _addedStrings.reserve(128);
    _workThreadUniqueHash.reserve(64);

}


cmRecord::~cmRecord(void)
{
    fclose(_fdChunks);
    delete[] _fileChunkBuffer;
}


// ================================================================
// Data access and cache management
// ================================================================

const bsVec<cmRecord::Evt>&
cmRecord::getEventChunk(chunkLoc_t pos, const bsVec<cmRecord::Evt>* lastLiveEvtChunk) const
{
    plgScope(ITCACHE, "getEventChunk");
    u64 idx = getChunkOffset(pos);
    plgData(ITCACHE, "Chunk index", idx);
    plgData(ITCACHE, "Cache size", _cacheAccess.size());

    // Is it the last data chunk not yet on file in case of live display?
    // No caching, as it is already in memory and will change often
    if(getChunkSize(pos)==0) {
        plAssert(lastLiveEvtChunk);
        return *lastLiveEvtChunk;
    }

    // In the cache?
    LRUIterator* ptr = _cacheAccess.find(idx);
    if(ptr) { // Yes
        _cacheLRU.splice(_cacheLRU.cbegin(), _cacheLRU, *ptr); // LRU thing. Just a move, no copy
        plgText(ITCACHE, "Location", "In cache");
        plAssert((*ptr)->isEvent, idx, recordByteQty);
        return (*ptr)->chunkEvent;
    }
    plgScope(ITCACHE, "Cache miss");

    // No. Cache full?
    if((int)_cacheAccess.size()>=_cacheMaxEntries) {
        plgData(ITCACHE, "Cache full, remove LRU index", _cacheLRU.back().chunkOffset);
        bool status = _cacheAccess.erase(_cacheLRU.back().chunkOffset); // Remove the Least Recently Used chunk
        plAssert(status);
        _cacheLRU.pop_back();
    }

    // Insert the new chunk
    _cacheLRU.push_front(CacheEntry( { idx } ));
    _cacheAccess.insert(idx, _cacheLRU.cbegin());
    CacheEntry& entry = _cacheLRU.front();
    entry.isEvent = true;
    auto& buf = entry.chunkEvent;

    // Populated it with data from disk
    buf.resize(cmChunkSize);
    plgBegin(ITCACHE, "Disk read");
    bsOsFseek(_fdChunks, idx, SEEK_SET);
    int expectedDiskSize = getChunkSize(pos);
    int finalBufferSize  = 0;
    if(compressionMode==1) {
        finalBufferSize = cmChunkSize*sizeof(Evt);
        plAssert(expectedDiskSize<=finalBufferSize);
        int fileSize = (int)fread(_fileChunkBuffer, 1, expectedDiskSize, _fdChunks);
        if(fileSize!=expectedDiskSize) { plMarker("weird", "Event chunk data read failed"); }
        plgBegin(ITCACHE, "Decompression");
        cmDecompressChunk(_fileChunkBuffer, fileSize, (u8*)&buf[0], &finalBufferSize);
        plgEnd(ITCACHE, "Decompression");
    } else {
        finalBufferSize = (int)fread(&buf[0], 1, expectedDiskSize, _fdChunks);
        if(finalBufferSize!=expectedDiskSize) { plMarker("weird", "Event chunk data read failed"); }
    }
    plgEnd(ITCACHE, "Disk read");
    if(finalBufferSize!=cmChunkSize*sizeof(Evt)) buf.resize(finalBufferSize/sizeof(Evt)); // May happen on the last chunk
    return buf;
}


const bsVec<u32>&
cmRecord::getElemChunk(chunkLoc_t pos, const bsVec<u32>* lastLiveLocChunk) const
{
    plgScope(ITCACHE, "getElemChunk");
    u64 idx = getChunkOffset(pos);
    plgData(ITCACHE, "Chunk index", idx);
    plgData(ITCACHE, "Cache size", _cacheAccess.size());

    // Is it the last data chunk not yet on file in case of live display?
    // No caching, as it is already in memory and will change often
    if(getChunkSize(pos)==0) {
        plAssert(lastLiveLocChunk);
        return *lastLiveLocChunk;
    }

    // In the cache?
    LRUIterator* ptr = _cacheAccess.find(idx);
    if(ptr) { // Yes
        _cacheLRU.splice(_cacheLRU.cbegin(), _cacheLRU, *ptr); // LRU thing. Just a move, no copy
        plgText(ITCACHE, "Location", "In cache");
        plAssert(!(*ptr)->isEvent, idx, recordByteQty);
        return (*ptr)->chunkElem;
    }
    plgScope(ITCACHE, "Cache miss");
    // No. Cache full?
    if((int)_cacheAccess.size()>=_cacheMaxEntries) {
        plgData(ITCACHE, "Cache full, remove LRU index", _cacheLRU.back().chunkOffset);
        _cacheAccess.erase(_cacheLRU.back().chunkOffset); // Remove the Least Recently Used chunk
        _cacheLRU.pop_back();
    }
    // Insert the new chunk
    _cacheLRU.push_front(CacheEntry( { idx } ));
    _cacheAccess.insert(idx,_cacheLRU.cbegin());
    CacheEntry& entry = _cacheLRU.front();
    entry.isEvent = false;
    auto& buf = entry.chunkElem;
    // Populated it with data from disk
    buf.resize(cmElemChunkSize);
    plgBegin(ITCACHE, "Disk read");
    bsOsFseek(_fdChunks, idx, SEEK_SET);
    int expectedDiskSize = getChunkSize(pos);
    int finalBufferSize  = 0;
    if(compressionMode==1) {
        finalBufferSize = cmElemChunkSize*sizeof(u32);
        plAssert(expectedDiskSize<=finalBufferSize);
        int fileSize = (int)fread(_fileChunkBuffer, 1, expectedDiskSize, _fdChunks);
        if(fileSize!=expectedDiskSize) { plMarker("weird", "Event chunk data read failed"); }
        plgBegin(ITCACHE, "Decompression");
        cmDecompressChunk(_fileChunkBuffer, fileSize, (u8*)&buf[0], &finalBufferSize);
        plgEnd(ITCACHE, "Decompression");
    } else {
        finalBufferSize = (int)fread(&buf[0], 1, expectedDiskSize, _fdChunks);
        if(finalBufferSize!=expectedDiskSize) { plMarker("weird", "Event chunk data read failed"); }
    }
    plgEnd(ITCACHE, "Disk read");
    if(finalBufferSize!=cmElemChunkSize*sizeof(u32)) buf.resize(finalBufferSize/sizeof(u32)); // May happen on the last chunk
    return buf;
}


void
cmRecord::getMemorySnapshot(int threadId, int snapshotIdx, bsVec<u32>& currentAllocMIdxs) const
{
    plAssert(threadId<threads.size());
    const bsVec<MemSnapshot>& memSnapshotIndexes = threads[threadId].memSnapshotIndexes;
    plAssert(snapshotIdx<memSnapshotIndexes.size());
    currentAllocMIdxs.clear();
    u64 pos = memSnapshotIndexes[snapshotIdx].fileLoc;
    bsOsFseek(_fdChunks, getChunkOffset(pos), SEEK_SET);
    // Read the quantity of allocations in the snapshot
    u32 allocatedScopeQty = 0;
    if((int)fread(&allocatedScopeQty, 4, 1, _fdChunks)!=1) return;
    currentAllocMIdxs.resize(allocatedScopeQty);
    // Read the allocations
    if(allocatedScopeQty) {
        if(compressionMode==0) {
            plAssert(getChunkSize(pos)==(int)((1+allocatedScopeQty)*sizeof(u32)),
                     getChunkSize(pos), allocatedScopeQty, (int)((1+allocatedScopeQty)*sizeof(u32)));
            if(fread(&currentAllocMIdxs[0], sizeof(u32), allocatedScopeQty, _fdChunks)!=allocatedScopeQty) currentAllocMIdxs.clear();
        }
        else {
            _workingBuffer.resize(getChunkSize(pos)-sizeof(u32)); // Substract the "allocatedScopeQty" integer size
            if((int)fread(&_workingBuffer[0], 1, _workingBuffer.size(), _fdChunks)!=_workingBuffer.size()) {
                currentAllocMIdxs.clear(); return;
            }
            int finalBufferSize = allocatedScopeQty*sizeof(u32); // Output buffer size (that We know to be the decompressed size)
            cmDecompressChunk(&_workingBuffer[0], _workingBuffer.size(), (u8*)&currentAllocMIdxs[0], &finalBufferSize);
            plAssert(finalBufferSize==(int)(allocatedScopeQty*sizeof(u32)));
        }
    }
}


// ================================================================
// Operations on strings
// ================================================================

void
cmRecord::loadExternalStrings(void)
{
    // Find the hash->string lookup file (per record or application default)
    _extStringsHashToStrIdx.clear();
    _extStrings.clear();
#define ADD_STRING(h, s) _extStringsHashToStrIdx.insert((u32)(h), h, _extStrings.size()); _extStrings.push_back(s)

    // Build the lookup
    ADD_STRING(2166136261+streams[0].tlvs[PL_TLV_HAS_HASH_SALT], "");         // Empty string 32 bits hash (FNV 32 offset)
    ADD_STRING(BS_FNV_HASH_OFFSET+streams[0].tlvs[PL_TLV_HAS_HASH_SALT], ""); // Empty string 64 bits hash

    // Read the file content
    bsVec<u8> b;
    if(!osLoadFileContent(recordPath.subString(0, recordPath.size()-4)+"_externalStrings", b)) {
        return; // This is normal if no external string lookup has been provided
    }

    int offset = 0;
    while(offset<=b.size()-20) { // 20 = 4 'at' symbols + 16 hash digits
        // Parse
        while(offset<=b.size()-2 && (b[offset]!='@' || b[offset+1]!='@')) ++offset;
        if(offset>b.size()-20) break;
        u64 key;
        if(sscanf((char*)&b[offset], "@@%16" PRIX64 "@@", &key)!=1) {
            while(offset<b.size() && b[offset]!='\n') ++offset;
            continue;
        }
        offset += 20;
        int startOffset = offset;
        while(offset<b.size() && b[offset]!='\n') ++offset;
        // Store the entry
        ADD_STRING(key, bsString((char*)&b[startOffset], (char*)&b[offset]));
    }
}


void
cmRecord::updateString(int strIdx)
{
    cmRecord::String& s = _strings[strIdx];
    // Resolve it, if needed
    if(s.isExternal) {
        u64  targetKey    = s.hash;
        int* extStringIdx = _extStringsHashToStrIdx.find((u32)targetKey, targetKey);
        if(extStringIdx) {
            s.value = _extStrings[*extStringIdx]; // Replace the string value
        } else {
            s.value.resize(21);
            snprintf((char*)&s.value[0], 21, "@@%016" PRIX64 "@@", targetKey);  // Hash not found in the provided list: string is "<hash>"
        }
    }

    // Extract the unit
    bsString& sv = s.value;
    int delimiterIdx = 0; // Find the unit delimiter "##"
    while(delimiterIdx+1<sv.size() && (sv[delimiterIdx]!='#' || sv[delimiterIdx+1]!='#')) ++delimiterIdx;
    if(delimiterIdx+1<sv.size()) {
        // Copy the unit and remove it from the original string
        s.unit = sv.subString(delimiterIdx+2).strip();
        sv[delimiterIdx] = 0;
        sv.resize(delimiterIdx+1);
    } else s.unit = "";
    sv.strip();

    // "hexa" is a special unit, displayed in hexadecimal
    s.isHexa = !strcmp("hexa", s.unit.toChar());

    // Count the lines
    for(int i=0; i<sv.size(); ++i) if(sv[i]=='\n') ++s.lineQty;
}


void
cmRecord::updateThreadString(int tId)
{
    char tmpStr[256];

    // Ensure thread name is valid and extract thread group if any
    Thread& rt = threads[tId];

    // Skip the group search if groups have already been processed
    if(rt.groupNameIdx>=0) return;

    // Sanity: if no name is provided, we provide a canonical one.
    if(rt.nameIdx<0) {
        rt.nameIdx      = FLAG_ADDED_STRING | _addedStrings.size();
        rt.groupNameIdx = -1;
        if(isMultiStream) snprintf(tmpStr, sizeof(tmpStr), "%s: Thread %d", streams[rt.streamId].appName.toChar(), tId);
        else              snprintf(tmpStr, sizeof(tmpStr), "Thread %d", tId);
        _addedStrings.push_back( { tmpStr, "", bsHashString(tmpStr), 0LL, 0, 1, -1, -1, false, false } );
        return; // No need to search for groups, and no update of threadUniqueHash
    }

    // Multistream: prefix the thread name with the app name
    if(isMultiStream) {
        snprintf(tmpStr, sizeof(tmpStr), "%s: %s", streams[rt.streamId].appName.toChar(), _strings[rt.nameIdx].value.toChar());
        rt.nameIdx      = FLAG_ADDED_STRING | _addedStrings.size();
        rt.groupNameIdx = -1;
        _addedStrings.push_back( { tmpStr, "", bsHashString(tmpStr), 0LL, 0, 1, -1, -1, false, false } );
    }

    // Copy the thread name hash inside the thread, for convenience
    rt.threadUniqueHash = getString(rt.nameIdx).hash;

    // Check for duplicated thread names. The table has only 1 instance of each hash
    bool isDuplicated = false;
    while(_workThreadUniqueHash.size()<=tId) _workThreadUniqueHash.push_back(0);  // 0 is a non-hash
    for(int tId2=0; tId2<_workThreadUniqueHash.size(); ++tId2) {
        if(tId2==tId || _workThreadUniqueHash[tId2]!=rt.threadUniqueHash) continue;
        isDuplicated = true; break;
    }
    // In case of duplicate, replace the name with a unique one
    if(isDuplicated) {
        snprintf(tmpStr, sizeof(tmpStr), "%s#%d", getString(rt.nameIdx).value.toChar(), tId);
        rt.nameIdx = FLAG_ADDED_STRING | _addedStrings.size();
        _addedStrings.push_back( { tmpStr, "", bsHashString(tmpStr), 0LL, 0, 1, -1, -1, false, false } );
        rt.threadUniqueHash = _addedStrings.back().hash;
    }
    else _workThreadUniqueHash[tId] = rt.threadUniqueHash;

    // Search for first "/" (1 level group only)
    const bsString& sv = getString(rt.nameIdx).value;
    int delimiterIdx = 0; // Find the unit delimiter '/'
    while(delimiterIdx<sv.size()-1 && sv[delimiterIdx]!='/') ++delimiterIdx;
    if(delimiterIdx>=sv.size()-1) return; // No group found. Note that unresolved external strings have no group

    // A group has been found: point to an existing added string or create one
    bsString groupName = sv.subString(0, delimiterIdx).strip();
    rt.groupNameIdx    = -1;
    for(int i=0; i<_addedStrings.size(); ++i) {
        if(_addedStrings[i].value==groupName) { rt.groupNameIdx = FLAG_ADDED_STRING | i; break; }
    }
    if(rt.groupNameIdx<0) { // New group: add the thread group name as an added string
        rt.groupNameIdx = FLAG_ADDED_STRING | _addedStrings.size();
        _addedStrings.push_back( { groupName, "", bsHashString(groupName.toChar()), 0LL, 0, 1, -1, -1, false, false } );
    }

    // Do not modify the initial thread string but replace it with a string without the group name
    const bsString& sv2 = getString(rt.nameIdx).value;
    rt.nameIdx = FLAG_ADDED_STRING | _addedStrings.size();
    bsString pureThreadName = sv2.subString(delimiterIdx+1, sv2.size()).strip();
    _addedStrings.push_back( { pureThreadName, "", bsHashString(pureThreadName.toChar()), 0LL, 0, 1, -1, -1, false, false } );
}


void
cmRecord::sortStrings(void)
{
    // Create the alphabetical string ordering (used as sorting key in some tables)
    // ============================================================================
    bsVec<int> stringIndex(_strings.size());
    for(int i=0; i<_strings.size(); ++i) stringIndex[i] = i;
    std::sort(stringIndex.begin(), stringIndex.end(),
              [this](const int a, const int b)->bool { return strcasecmp(_strings[a].value.toChar(), _strings[b].value.toChar())<0; } );
    for(int i=0; i<_strings.size(); ++i) _strings[stringIndex[i]].alphabeticalOrder = i;
}


// ================================================================
// Live update of a record
// ================================================================

void
cmRecord::Delta::reset(void)
{
    errorQty = 0;
    coreUsageChunkLocs.clear();
    coreUsageLastLiveEvtChunk.clear();
    markerChunkLocs.clear();
    markerLastLiveEvtChunk.clear();
    lockNtfChunkLocs.clear();
    lockNtfLastLiveEvtChunk.clear();
    lockUseChunkLocs.clear();
    lockUseLastLiveEvtChunk.clear();
    locks.clear();
    threads.clear();
    elems.clear();
    markerCategories.clear();
    strings.clear();
    updatedThreadIds.clear();
    updatedElemIds.clear();
    updatedLockIds.clear();
    updatedStrings.clear();
}


bool
cmRecord::updateFromDelta(cmRecord::Delta* delta)
{
    bool doNeedConfigUpdate = false;

    // Statistics
    durationNs     = delta->durationNs;
    recordByteQty  = delta->recordByteQty;  // This is the last offset in the record file
    coreQty        = delta->coreQty;
    elemEventQty   = delta->elemEventQty;
    memEventQty    = delta->memEventQty;
    ctxSwitchEventQty = delta->ctxSwitchEventQty;
    lockEventQty   = delta->lockEventQty;
    markerEventQty = delta->markerEventQty;

    // Size (=0) is a marker/sentinel for the "end of chunk"
    // When this size is found, the "live data chunk" is used instead of the disk content
    chunkLoc_t endChunkLoc = cmRecord::makeChunkLoc(recordByteQty, 0);

    // Errors
    if(delta->errorQty>0) {
        plAssert(errorQty+delta->errorQty<=MAX_REC_ERROR_QTY);
        memcpy(&errors[errorQty], &delta->errors[0], delta->errorQty*sizeof(cmRecord::RecError));
        errorQty += delta->errorQty;
    }

    // New strings
    if(!delta->strings.empty()) {
        for(const String& s : delta->strings) {
            _strings.push_back(s);
            updateString(_strings.size()-1); // External + unit extraction + line count
        }
        sortStrings();
    }

    // Stream app names
    for(int i=streams.size(); i<delta->streams.size(); ++i) {
        streams.push_back(delta->streams[i]);
    }
    plAssert(streams.size()==delta->streams.size());

    // Updated strings
    for(const DeltaString& src : delta->updatedStrings) {
        String& dst            = _strings[src.stringId];
        dst.threadBitmapAsName = src.threadBitmapAsName;
        dst.lockId             = src.lockId;
        dst.categoryId         = src.categoryId;
    }
    delta->updatedStrings.clear();

    // Marker categories
    for(int i=markerCategories.size(); i<delta->markerCategories.size(); ++i) {
        markerCategories.push_back(delta->markerCategories[i]);
    }
    plAssert(markerCategories.size()==delta->markerCategories.size());

    // New locks
    for(int i=locks.size(); i<delta->locks.size(); ++i) {
        locks.push_back(delta->locks[i]);
    }
    plAssert(locks.size()==delta->locks.size());

    // Lock waiting thread list update
    for(int updatedLockId : delta->updatedLockIds) {
        const Lock& src = delta->locks[updatedLockId];
        Lock&       dst = locks[updatedLockId];
        dst.waitingThreadIds.reserve(src.waitingThreadIds.size());
        for(int i=dst.waitingThreadIds.size(); i<src.waitingThreadIds.size(); ++i) {
            dst.waitingThreadIds.push_back(src.waitingThreadIds[i]);
        }
    }
    delta->updatedLockIds.clear();

    // New threads
    doNeedConfigUpdate = doNeedConfigUpdate || threads.size()!=delta->threads.size();
    for(int i=threads.size(); i<delta->threads.size(); ++i) {
        threads.push_back({});
        const Thread& src = delta->threads[i];
        Thread&       dst = threads.back();
        dst.threadHash       = src.threadHash;
        dst.threadUniqueHash = src.threadUniqueHash;
        dst.nameIdx          = src.nameIdx;
        dst.streamId         = src.streamId;
        updateThreadString(i);
    }
    plAssert(threads.size()==delta->threads.size());

    // Thread name update
    doNeedConfigUpdate = doNeedConfigUpdate || !delta->updatedThreadIds.empty();
    for(int updatedTId : delta->updatedThreadIds) {
        threads[updatedTId].nameIdx          = delta->threads[updatedTId].nameIdx;
        threads[updatedTId].threadUniqueHash = delta->threads[updatedTId].threadUniqueHash;
        threads[updatedTId].groupNameIdx     = -1;  // Need to reset it
        updateThreadString(updatedTId);
    }
    delta->updatedThreadIds.clear();

#define UPDATE_FROM_DELTA(s, d, name)                                   \
    if(!d.name##LastLiveEvtChunk.empty()) d.name##ChunkLocs.pop_back(); /* Fake pos removed before update */ \
    if(!s.name##ChunkLocs.empty()) {                                    \
        d.name##ChunkLocs.resize(d.name##ChunkLocs.size()+s.name##ChunkLocs.size()); \
        memcpy(&d.name##ChunkLocs[d.name##ChunkLocs.size()-s.name##ChunkLocs.size()], &s.name##ChunkLocs[0], s.name##ChunkLocs.size()*sizeof(chunkLoc_t)); \
    }                                                                   \
    if(!s.name##LastLiveEvtChunk.empty()) d.name##ChunkLocs.push_back(endChunkLoc); /* Fake pos added to reach the live last chunk */ \
    if(!s.name##ChunkLocs.empty() || d.name##LastLiveEvtChunk.size()!=s.name##LastLiveEvtChunk.size()) { \
        d.name##LastLiveEvtChunk.resize(s.name##LastLiveEvtChunk.size()); \
        if(!s.name##LastLiveEvtChunk.empty()) memcpy(&d.name##LastLiveEvtChunk[0], &s.name##LastLiveEvtChunk[0], s.name##LastLiveEvtChunk.size()*sizeof(cmRecord::Evt)); \
    }

    // Update threads
    for(int i=0; i<threads.size(); ++i) {
        cmRecord::Thread& src = delta->threads[i];
        cmRecord::Thread& dst = threads[i];

        // Thread stats
        dst.durationNs   = src.durationNs;
        dst.elemEventQty = src.elemEventQty;
        dst.memEventQty  = src.memEventQty;
        dst.ctxSwitchEventQty = src.ctxSwitchEventQty;
        dst.lockEventQty = src.lockEventQty;
        dst.markerEventQty = src.markerEventQty;

        // Update thread levels
        for(int j=dst.levels.size(); j<src.levels.size(); ++j) dst.levels.push_back({}); // New levels
        for(int j=0; j<src.levels.size(); ++j) {
            cmRecord::NestingLevel& lsrc = src.levels[j];
            cmRecord::NestingLevel& ldst = dst.levels[j];

            // Scope and non scope chunks
            UPDATE_FROM_DELTA(lsrc, ldst, nonScope);
            UPDATE_FROM_DELTA(lsrc, ldst, scope);

            // MR levels
            for(int k=ldst.mrScopeSpeckChunks.size(); k<lsrc.mrScopeSpeckChunks.size(); ++k) {  // New MR levels
                ldst.mrScopeSpeckChunks.push_back({});
            }
            for(int k=0; k<lsrc.mrScopeSpeckChunks.size(); ++k) {
                bsVec<u32>& lmsrc = lsrc.mrScopeSpeckChunks[k];
                if(lmsrc.empty()) continue;
                bsVec<u32>& lmdst = ldst.mrScopeSpeckChunks[k];
                lmdst.resize(lmdst.size()+lmsrc.size());
                memcpy(&lmdst[lmdst.size()-lmsrc.size()], &lmsrc[0], lmsrc.size()*sizeof(u32));
                lmsrc.clear();
            }
        } // src.levels

        UPDATE_FROM_DELTA(src, dst, memAlloc);
        UPDATE_FROM_DELTA(src, dst, memDealloc);
        UPDATE_FROM_DELTA(src, dst, memPlot);
        UPDATE_FROM_DELTA(src, dst, ctxSwitch);
        UPDATE_FROM_DELTA(src, dst, softIrq);
        UPDATE_FROM_DELTA(src, dst, lockWait);

        // Update memory specific storage (only delta are copied)
        if(!src.memDeallocMIdx.empty()) {
            dst.memDeallocMIdx.resize(dst.memDeallocMIdx.size()+src.memDeallocMIdx.size());
            memcpy(&dst.memDeallocMIdx[dst.memDeallocMIdx.size()-src.memDeallocMIdx.size()],
                   &src.memDeallocMIdx[0], src.memDeallocMIdx.size()*sizeof(u32));
        }
        if(!src.memSnapshotIndexes.empty()) {
            dst.memSnapshotIndexes.resize(dst.memSnapshotIndexes.size()+src.memSnapshotIndexes.size());
            memcpy(&dst.memSnapshotIndexes[dst.memSnapshotIndexes.size()-src.memSnapshotIndexes.size()],
                   &src.memSnapshotIndexes[0], src.memSnapshotIndexes.size()*sizeof(MemSnapshot));
        }
    } // threads

    UPDATE_FROM_DELTA((*delta), (*this), lockUse);
    UPDATE_FROM_DELTA((*delta), (*this), lockNtf);
    UPDATE_FROM_DELTA((*delta), (*this), coreUsage);
    UPDATE_FROM_DELTA((*delta), (*this), marker);

    // New elems
    doNeedConfigUpdate = doNeedConfigUpdate || elems.size()!=delta->elems.size();
    for(int i=elems.size(); i<delta->elems.size(); ++i) {
        const Elem& src = delta->elems[i];
        elemPathToId.insert(src.hashPath, src.hashKey, i);
        elems.push_back({src.hashPath, src.partialHashPath, src.threadBitmap, src.hashKey, src.prevElemIdx, src.threadId, src.nestingLevel,
                src.nameIdx, src.hlNameIdx, src.flags, src.isPartOfHStruct, src.isThreadHashed, src.absYMin, src.absYMax});
    }
    plAssert(elems.size()==delta->elems.size());

    // Elem content update
    doNeedConfigUpdate = doNeedConfigUpdate || !delta->updatedElemIds.empty();
    for(int updatedElemId : delta->updatedElemIds) {
        Elem& src = delta->elems[updatedElemId];
        Elem& dst = elems[updatedElemId];

        // Update attributes
        dst.threadBitmap = src.threadBitmap;
        dst.absYMin      = src.absYMin;
        dst.absYMax      = src.absYMax;

        // Update locations
        if(!dst.lastLiveLocChunk.empty()) dst.chunkLocs.pop_back(); /* Fake pos removed before update */
        if(!src.chunkLocs.empty()) {
            dst.chunkLocs.resize(dst.chunkLocs.size()+src.chunkLocs.size());
            memcpy(&dst.chunkLocs[dst.chunkLocs.size()-src.chunkLocs.size()], &src.chunkLocs[0], src.chunkLocs.size()*sizeof(chunkLoc_t));
        }
        if(!src.lastLiveLocChunk.empty()) dst.chunkLocs.push_back(endChunkLoc); /* Fake pos added to reach the live last chunk */
        if(!src.chunkLocs.empty() || dst.lastLiveLocChunk.size()!=src.lastLiveLocChunk.size()) {
            dst.lastLiveLocChunk.resize(src.lastLiveLocChunk.size());
            if(!src.lastLiveLocChunk.empty()) memcpy(&dst.lastLiveLocChunk[0], &src.lastLiveLocChunk[0], src.lastLiveLocChunk.size()*sizeof(u32));
        }

        // Update MR levels
        for(int k=dst.mrSpeckChunks.size(); k<src.mrSpeckChunks.size(); ++k) {  // New MR levels
            dst.mrSpeckChunks.push_back({});
        }
        for(int k=0; k<src.mrSpeckChunks.size(); ++k) {
            bsVec<ElemMR>& msrc = src.mrSpeckChunks[k];
            if(msrc.empty()) continue;
            bsVec<ElemMR>& mdst = dst.mrSpeckChunks[k];
            mdst.resize(mdst.size()+msrc.size());
            memcpy(&mdst[mdst.size()-msrc.size()], &msrc[0], msrc.size()*sizeof(ElemMR));
            msrc.clear();
        }
    }
    delta->updatedElemIds.clear();

    return doNeedConfigUpdate;
}


void
cmRecord::buildMarkerCategories(void)
{
    // Loop on threads and categories
    for(int tId=0; tId<threads.size(); ++tId) {
        const Thread& t = threads[tId];
        for(int categoryId=0; categoryId<markerCategories.size(); ++categoryId) {
            u64 itemHashPath = bsHashStepChain(t.threadHash, markerCategories[categoryId], cmConst::MARKER_NAMEIDX);
            int* elemIdxPtr  = elemPathToId.find(itemHashPath, cmConst::MARKER_NAMEIDX);
            if(elemIdxPtr) markerElems.push_back( { *elemIdxPtr, tId, categoryId } );
        }
    }
}


// ================================================================
// Load record
// ================================================================

cmRecord*
cmLoadRecord(const bsString& path, int cacheMBytes, bsString& errorMsg)
{
    constexpr int SANE_MAX_ELEMENT_QTY = 1000000; // Maximum entity kind quantity, with a margin. For robustness only
    constexpr int SANE_MAX_EVENT_QTY   = 2147483647; // 2^31 - 1

    // Init and macro definitions
    errorMsg.clear();
#define LOAD_ERROR(msg) do {                    \
        errorMsg = "unable to " msg;            \
        delete record;                          \
        return 0;                               \
    } while(0)
#define READ_INT(varName, errorMsg)  if((int)fread(&varName, 4, 1, recFd)!=1) LOAD_ERROR(errorMsg)


    // Open the record file
    cmRecord* record = 0;
    FILE* recFd = osFileOpen(path, "rb");
    if(!recFd) LOAD_ERROR("open the record file");
    record = new cmRecord(recFd, cacheMBytes); // Ownership of fds transferred (but still used in this function to initialize the record)
    plAssert(record);
    record->recordDate = osGetCreationDate(path);
    int length;
    int threadQty = 0;

    // Check the bootstrap and point on the meta informations
    if(bsOsFseek(recFd, -16L, SEEK_END)!=0) LOAD_ERROR("find the record bootstrap");
    char magic[9];
    if((int)fread(&magic, 1, 8, recFd)!=8) LOAD_ERROR("read the magic identifier");
    if(strncmp(magic, "PL-MAGIC", 8)) LOAD_ERROR("match a Palanteer file type");
    s64 headerStartOffset;
    if((int)fread(&headerStartOffset, 8, 1, recFd)!=1) LOAD_ERROR("read the meta information location");
    if(bsOsFseek(recFd, headerStartOffset, SEEK_SET)!=0) LOAD_ERROR("find the meta information");

    // Format version
    int formatVersion = 0;
    READ_INT(formatVersion, "read the format version");
    if(formatVersion!=PL_RECORD_FORMAT_VERSION) LOAD_ERROR("handle the unsupported format version."); // Later, it can be used to support several versions
    // Application name
    READ_INT(length, "read the app name size");
    if(length<=0 || length>1024) LOAD_ERROR("handle the abnormal app name size"); // Cannot be empty because set to "<no name>" in this case in cmCnx
    record->appName.resize(length);
    if((int)fread(&record->appName[0], 1, length, recFd)!=length) LOAD_ERROR("read the app name");
    // Thread quantity
    READ_INT(threadQty, "read the thread qty");
    if((u32)threadQty>255) LOAD_ERROR("handle the abnormal thread quantity");
    // Core quantity (usable only if context switches have been collected)
    READ_INT(record->coreQty, "read the core qty");
    if((u32)record->coreQty>128) LOAD_ERROR("handle the abnormal core quantity");
    // String quantity
    READ_INT(length, "read the string qty");
    if(length<0) LOAD_ERROR("handle the abnormal string quantity");
    bsVec<cmRecord::String>& strings = record->getStrings();
    strings.resize(length);
    // Compression mode
    READ_INT(record->compressionMode, "read the compression mode");
    if(record->compressionMode<0 || record->compressionMode>1) LOAD_ERROR("handle the abnormal compression mode");
    // Multistream mode
    READ_INT(record->isMultiStream, "read the multistream mode");
    if(record->isMultiStream<0 || record->isMultiStream>1) LOAD_ERROR("handle the abnormal multistream mode");

    record->recordPath = path;
    record->recordByteQty = osGetSize(path);
    record->threads.resize(threadQty);
    record->durationNs = 0;

    // Read the global statistics
    READ_INT(record->elemEventQty,      "read the thread elem event quantity");
    READ_INT(record->memEventQty,       "read the thread mem  event quantity");
    READ_INT(record->ctxSwitchEventQty, "read the thread context switch event quantity");
    READ_INT(record->lockEventQty,      "read the thread lock event quantity");
    READ_INT(record->markerEventQty,    "read the thread marker event quantity");

    // Read the streams
    READ_INT(length, "read the stream quantity");
    if(length<0 || length>cmConst::MAX_STREAM_QTY) LOAD_ERROR("handle the abnormal stream quantity");
    record->streams.resize(length);
    for(cmStreamInfo& si : record->streams) {
        // App, build and lang names
        READ_INT(length, "read the stream app name length");
        if(length<0 || length>1024) LOAD_ERROR("handle the abnormal stream app name length");
        si.appName.resize(length);
        if(length && (int)fread(&si.appName[0], 1, length, recFd)!=length) LOAD_ERROR("read the stream app name content");
        READ_INT(length, "read the stream build name length");
        if(length<0 || length>1024) LOAD_ERROR("handle the abnormal stream build name length");
        si.buildName.resize(length);
        if(length && (int)fread(&si.buildName[0], 1, length, recFd)!=length) LOAD_ERROR("read the stream build name content");
        READ_INT(length, "read the stream lang name length");
        if(length<0 || length>1024) LOAD_ERROR("handle the abnormal stream lang name length");
        si.langName.resize(length);
        if(length && (int)fread(&si.langName[0], 1, length, recFd)!=length) LOAD_ERROR("read the stream lang name content");
        // TLVs
        memset(&si.tlvs[0], 0, sizeof(si.tlvs));
        READ_INT(length, "read the options size");
        if(length<0 || length>=32) LOAD_ERROR("handle the abnormal options size");
        length = bsMin(length, PL_TLV_QTY);
        if((int)fread(&si.tlvs[0], 8, length, recFd)!=length) LOAD_ERROR("read the stream tlvs");
    }

    // Read the strings
    for(cmRecord::String& s : strings) {
        READ_INT(length, "read the string length");
        if(length<0 || length>1024) LOAD_ERROR("handle the abnormal string length");
        s.value.resize(length);
        if(length && (int)fread(&s.value[0], 1, length, recFd)!=length) LOAD_ERROR("read the string content");
        if((int)fread(&s.hash, 8, 1, recFd)!=1) LOAD_ERROR("read the hash string");
        if((int)fread(&s.threadBitmapAsName, 8, 1, recFd)!=1) LOAD_ERROR("read the string thread bitmap as name");
        s.alphabeticalOrder = 0;
        s.lineQty = 1;
        READ_INT(s.lockId, "read the string lock Id");
        READ_INT(s.categoryId, "read the string category Id");
        s.isExternal = (s.value.size()==1); // Equal to a null termination
        s.isHexa = false;
    }

    // Loop on threads
    for(int tId=0; tId<threadQty; ++tId) {
        cmRecord::Thread& rt = record->threads[tId];
        READ_INT(rt.streamId,          "read the thread stream Id");
        if(rt.streamId<0 || rt.streamId>=record->streams.size()) LOAD_ERROR("handle the abnormal thread stream ID");
        READ_INT(rt.nameIdx,           "read the thread name idx");
        if((int)fread(&rt.threadHash, 8, 1, recFd)!=1) LOAD_ERROR("read the thread hash");
        if((int)fread(&rt.durationNs, 8, 1, recFd)!=1) LOAD_ERROR("read the thread end date");
        if(rt.durationNs>record->durationNs) record->durationNs = rt.durationNs;
        READ_INT(rt.elemEventQty,      "read the thread elem event quantity");
        READ_INT(rt.memEventQty,       "read the thread mem  event quantity");
        READ_INT(rt.ctxSwitchEventQty, "read the thread context switch event quantity");
        READ_INT(rt.lockEventQty,      "read the thread lock event quantity");
        READ_INT(rt.markerEventQty,    "read the thread marker event quantity");

        // Nesting level quantity
        int nestingLevelQty;
        READ_INT(nestingLevelQty, "read the thread nesting level");
        if(nestingLevelQty<0 || nestingLevelQty>1024) LOAD_ERROR("handle the abnormal nesting level");

        // Loop on nesting levels
        rt.levels.resize(nestingLevelQty);
        for(int nLevel=0; nLevel<nestingLevelQty; ++nLevel) {
            cmRecord::NestingLevel& nl = rt.levels[nLevel];
            // Chunk indexes for this nesting level
            int chunkQty;
            READ_INT(chunkQty, "read the non-scope chunk quantity for this level");
            if(chunkQty<0 || chunkQty>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal non-scope chunk qty for this nesting level");
            else if(chunkQty>0) {
                nl.nonScopeChunkLocs.resize(chunkQty);
                if((int)fread(&nl.nonScopeChunkLocs[0], sizeof(chunkLoc_t), chunkQty, recFd)!=chunkQty)
                    LOAD_ERROR("read the non-scope chunk indexes");
            }
            READ_INT(chunkQty, "read the scope chunk quantity for this level");
            if(chunkQty<0 || chunkQty>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal scope chunk qty for this nesting level");
            else if(chunkQty>0) {
                nl.scopeChunkLocs.resize(chunkQty);
                if((int)fread(&nl.scopeChunkLocs[0], sizeof(chunkLoc_t), chunkQty, recFd)!=chunkQty)
                    LOAD_ERROR("read the scope chunk indexes");
            }
            // Multi-resolution level quantity
            int mrLevelQty;
            READ_INT(mrLevelQty, "read the MR level qty");
            if(mrLevelQty<0 || mrLevelQty>64)
                LOAD_ERROR("handle the abnormal multi-resolution level for this nesting level");
            bsVec<bsVec<u32>>& mrArrays = nl.mrScopeSpeckChunks;
            mrArrays.resize(mrLevelQty);
            // Loop on mr levels
            for(int mrLevel=0; mrLevel<mrLevelQty; ++mrLevel) {
                int size;
                READ_INT(size, "read the MR level size");
                if(size<0 || size>SANE_MAX_EVENT_QTY/cmMRScopeSize) LOAD_ERROR("handle the abnormal multi-resolution buffer size");
                mrArrays[mrLevel].resize(size);
                if(!size) { mrArrays.resize(mrLevel); break; }
                if((int)fread(&mrArrays[mrLevel][0], sizeof(u32), size, recFd)!=size)
                    LOAD_ERROR("read the MR level array");
            }

            // Some multi-resolution scopes integrity checks for this hierarchical level of the thread (so that iterators stay safe)
            for(int mrLevel=0; mrLevel<mrArrays.size()-1; ++mrLevel) {
                const bsVec<u32>& curArray   = mrArrays[mrLevel];
                const bsVec<u32>& upperArray = mrArrays[mrLevel+1];
                // Check 1: both arrays are not empty
                if(curArray.empty() || upperArray.empty()) LOAD_ERROR("handle the abnormal scope empty MR level");
                // Check 2: each MR level has a parent in the upper array (pyramidal construction)
                if((curArray.size()+cmMRScopeSize-1)/cmMRScopeSize!=upperArray.size()) LOAD_ERROR("handle the non pyramidal scope MR structure");
                // Check 3: each MR element shall have smaller speck size than upper element
                for(int i=0; i<curArray.size(); ++i) {
                    if(curArray[i]>upperArray[i/cmMRScopeSize]) LOAD_ERROR("handle the scope MR non increasing speck");
                }
            }

        } // for(int nLevel...

        // Load the memory event indexes
        int mcq; // memory chunk quantity
        READ_INT(mcq, "read the memory alloc chunk quantity");
        if(mcq<0 || mcq>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal memory alloc chunk qty");
        else if(mcq>0) {
            rt.memAllocChunkLocs.resize(mcq);
            if((int)fread(&rt.memAllocChunkLocs[0], sizeof(chunkLoc_t), mcq, recFd)!=mcq) LOAD_ERROR("read the memory alloc chunk indexes");
        }
        READ_INT(mcq, "read the memory dealloc chunk quantity");
        if(mcq<0 || mcq>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal memory dealloc chunk qty");
        else if(mcq>0) {
            rt.memDeallocChunkLocs.resize(mcq);
            if((int)fread(&rt.memDeallocChunkLocs[0], sizeof(chunkLoc_t), mcq, recFd)!=mcq) LOAD_ERROR("read the memory dealloc chunk indexes");
        }
        READ_INT(mcq, "read the memory plot chunk quantity");
        if(mcq<0 || mcq>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal memory plot chunk qty");
        else if(mcq>0) {
            rt.memPlotChunkLocs.resize(mcq);
            if((int)fread(&rt.memPlotChunkLocs[0], sizeof(chunkLoc_t), mcq, recFd)!=mcq) LOAD_ERROR("read the memory plot chunk indexes");
        }
        READ_INT(mcq, "read the memory dealloc lookup size");
        if(mcq<0) LOAD_ERROR("handle the abnormal memory dealloc lookup size");
        else if(mcq>0) {
            rt.memDeallocMIdx.resize(mcq);
            if((int)fread(&rt.memDeallocMIdx[0], sizeof(u32), mcq, recFd)!=mcq) LOAD_ERROR("read the memory dealloc lookup");
        }
        READ_INT(mcq, "read the memory snapshot index size");
        if(mcq<0 || mcq>SANE_MAX_EVENT_QTY/PL_MEMORY_SNAPSHOT_EVENT_INTERVAL) LOAD_ERROR("handle the abnormal memory snapshot index size");
        else if(mcq>0) {
            rt.memSnapshotIndexes.resize(mcq);
            if((int)fread(&rt.memSnapshotIndexes[0], sizeof(cmRecord::MemSnapshot), mcq, recFd)!=mcq) LOAD_ERROR("read the memory snapshot index");
        }
        READ_INT(mcq, "read the context switch chunk quantity");
        if(mcq<0 || mcq>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal context switch chunk qty");
        else if(mcq>0) {
            rt.ctxSwitchChunkLocs.resize(mcq);
            if((int)fread(&rt.ctxSwitchChunkLocs[0], sizeof(chunkLoc_t), mcq, recFd)!=mcq) LOAD_ERROR("read the context switch chunk indexes");
        }
        READ_INT(mcq, "read the SOFTIRQ chunk quantity");
        if(mcq<0 || mcq>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal SOFTIRQ chunk qty");
        else if(mcq>0) {
            rt.softIrqChunkLocs.resize(mcq);
            if((int)fread(&rt.softIrqChunkLocs[0], sizeof(chunkLoc_t), mcq, recFd)!=mcq) LOAD_ERROR("read the SOFTIRQ chunk indexes");
        }
        READ_INT(mcq, "read the lock wait chunk quantity");
        if(mcq<0 || mcq>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal lock wait chunk qty");
        else if(mcq>0) {
            rt.lockWaitChunkLocs.resize(mcq);
            if((int)fread(&rt.lockWaitChunkLocs[0], sizeof(chunkLoc_t), mcq, recFd)!=mcq) LOAD_ERROR("read the lock wait chunk indexes");
        }

    } // End of loop on threads

    READ_INT(length, "read the core use chunk quantity");
    if(length<0 || length>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal core use chunk qty");
    else if(length>0) {
        record->coreUsageChunkLocs.resize(length);
        if((int)fread(&record->coreUsageChunkLocs[0], sizeof(chunkLoc_t), length, recFd)!=length) LOAD_ERROR("read the core usage chunk indexes");
    }

    READ_INT(length, "read the marker chunk quantity");
    if(length<0 || length>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal marker chunk qty");
    else if(length>0) {
        record->markerChunkLocs.resize(length);
        if((int)fread(&record->markerChunkLocs[0], sizeof(chunkLoc_t), length, recFd)!=length) LOAD_ERROR("read the marker chunk indexes");
    }

    // Load the category list
    READ_INT(length, "read the marker category quantity");
    if(length<0) LOAD_ERROR("handle the abnormal marker category qty");
    else if(length>0) {
        record->markerCategories.resize(length);
        if((int)fread(&record->markerCategories[0], sizeof(int), length, recFd)!=length) LOAD_ERROR("read the marker category list");
    }

    READ_INT(length, "read the lock notification chunk quantity");
    if(length<0 || length>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal lock notification chunk qty");
    else if(length>0) {
        record->lockNtfChunkLocs.resize(length);
        if((int)fread(&record->lockNtfChunkLocs[0], sizeof(chunkLoc_t), length, recFd)!=length) LOAD_ERROR("read the lock notification chunk indexes");
    }

    READ_INT(length, "read the lock use chunk quantity");
    if(length<0 || length>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal lock use chunk qty");
    else if(length>0) {
        record->lockUseChunkLocs.resize(length);
        if((int)fread(&record->lockUseChunkLocs[0], sizeof(chunkLoc_t), length, recFd)!=length) LOAD_ERROR("read the lock use chunk indexes");
    }

    // Load the lock name array
    READ_INT(length, "read the lock array size");
    if(length<0 || length>SANE_MAX_ELEMENT_QTY) LOAD_ERROR("handle the abnormal lock array size");
    record->locks.resize(length);
    for(int lockIdx=0; lockIdx<record->locks.size(); ++lockIdx) {
        cmRecord::Lock& lock = record->locks[lockIdx];
        READ_INT(lock.nameIdx, "read the lock name index");
        READ_INT(length, "read the lock waiting threadId array size");
        if(length<0 || length>255) LOAD_ERROR("handle the abnormal lock waiting threadId array size");
        lock.waitingThreadIds.resize(length);
        if(length>0 && (int)fread(&lock.waitingThreadIds[0], sizeof(int), length, recFd)!=length)
            LOAD_ERROR("read the lock waiting threadId array");
    }

    u32 elemQty;
    READ_INT(elemQty, "read the elem quantity");
    if(elemQty>SANE_MAX_ELEMENT_QTY) LOAD_ERROR("handle the abnormal elem quantity");
    record->elems.resize(elemQty);

    // Read the elems
    for(u32 elemIdx=0; elemIdx<elemQty; ++elemIdx) {
        cmRecord::Elem& elem = record->elems[elemIdx];
        // Base information
        if((int)fread(&elem.hashPath,        8, 1, recFd)!=1) LOAD_ERROR("read the elem path");
        if((int)fread(&elem.partialHashPath, 8, 1, recFd)!=1) LOAD_ERROR("read the elem path");
        if((int)fread(&elem.threadBitmap,    8, 1, recFd)!=1) LOAD_ERROR("read the elem thread bitmap");
        READ_INT(elem.hashKey,      "read the elem hash key");
        READ_INT(elem.prevElemIdx,  "read the elem previous elem Id");
        if(elem.prevElemIdx!=PL_INVALID && elem.prevElemIdx>=elemQty) LOAD_ERROR("handle the abnormal elem previous elem Id");
        READ_INT(elem.threadId,     "read the elem thread Id");
        if(elem.threadId!=0xFFFF && elem.threadId>=cmConst::MAX_THREAD_QTY) LOAD_ERROR("handle the abnormal elem thread Id");
        READ_INT(elem.nestingLevel, "read the elem nesting level");
        if(elem.nestingLevel>=cmConst::MAX_LEVEL_QTY) LOAD_ERROR("handle the abnormal elem level quantity");
        READ_INT(elem.nameIdx,      "read the elem name");
        READ_INT(elem.hlNameIdx,    "read the elem highlight name");
        READ_INT(elem.flags,        "read the elem flags");
        READ_INT(elem.isPartOfHStruct, "read the elem boolean if part of hierarchical structure");
        READ_INT(elem.isThreadHashed,  "read the elem boolean if thread is hashed");
        if((int)fread(&elem.absYMin, 8, 1, recFd)!=1) LOAD_ERROR("read the absolute minimum value");
        if((int)fread(&elem.absYMax, 8, 1, recFd)!=1) LOAD_ERROR("read the absolute maximum value");
        record->elemPathToId.insert(elem.hashPath, elem.hashKey, elemIdx);

        // Chunk indexes for this elem
        int chunkQty;
        READ_INT(chunkQty, "read the elem chunk quantity");
        if(chunkQty<0 || chunkQty>SANE_MAX_EVENT_QTY/cmChunkSize) LOAD_ERROR("handle the abnormal elem chunk qty");
        else if(chunkQty>0) {
            elem.chunkLocs.resize(chunkQty);
            if((int)fread(&elem.chunkLocs[0], sizeof(chunkLoc_t), chunkQty, recFd)!=chunkQty)
                LOAD_ERROR("read the elem chunk indexes");
        }
        // Multi-resolution level quantity
        int mrLevelQty;
        READ_INT(mrLevelQty, "read the elem MR level");
        if(mrLevelQty<0 || mrLevelQty>64) LOAD_ERROR("handle the abnormal elem multi-resolution level");
        elem.mrSpeckChunks.resize(mrLevelQty);
        // Loop on mr levels
        for(int mrLevel=0; mrLevel<mrLevelQty; ++mrLevel) {
            int size;
            READ_INT(size, "read the elem MR level size");
            if(size<0 || size>SANE_MAX_EVENT_QTY/cmMRElemSize) LOAD_ERROR("handle the abnormal elem multi-resolution buffer size");
            elem.mrSpeckChunks[mrLevel].resize(size);
            if(!size) { elem.mrSpeckChunks.resize(mrLevel); break; }
            if((int)fread(&elem.mrSpeckChunks[mrLevel][0], sizeof(cmRecord::ElemMR), size, recFd)!=size)
                LOAD_ERROR("read the elem MR level array");
        }

        // Some multi-resolution  integrity checks for this "elem" (so that iterators stay safe)
        bsVec<bsVec<cmRecord::ElemMR>>& mrArrays = elem.mrSpeckChunks;
        for(int mrLevel=0; mrLevel<mrArrays.size()-1; ++mrLevel) {
            const bsVec<cmRecord::ElemMR>& curArray   = mrArrays[mrLevel];
            const bsVec<cmRecord::ElemMR>& upperArray = mrArrays[mrLevel+1];
            // Check 1: both arrays are not empty
            if(curArray.empty() || upperArray.empty()) LOAD_ERROR("handle the abnormal empty elem MR level");
            // Check 2: each MR level has a parent in the upper array (pyramidal construction)
            if((curArray.size()+cmMRElemSize-1)/cmMRElemSize!=upperArray.size()) LOAD_ERROR("handle the non pyramidal elem MR structure");
            // Check 3: each MR element shall have smaller speck size than upper element
            for(int i=0; i<curArray.size(); ++i) {
                if(curArray[i].speckUs>upperArray[i/cmMRElemSize].speckUs) LOAD_ERROR("check the elem MR non increasing speck");
            }
        }
    } // End of loop on Elems

    // Read the instrumentation errors
    READ_INT(record->errorQty, "read the logged instrumentation error quantity");
    if(record->errorQty>cmRecord::MAX_REC_ERROR_QTY) LOAD_ERROR("handle the abnormal logged instrumentation error quantity");
    else if(record->errorQty>0) {
        if(fread(&record->errors[0], sizeof(cmRecord::RecError), record->errorQty, recFd)!=record->errorQty)
            LOAD_ERROR("read the logged instrumentation errors");
    }

    // Manage external strings, units and thread groups
    record->loadExternalStrings();
    for(int sId=0; sId<strings.size(); ++sId) record->updateString(sId);
    for(int tId=0; tId<record->threads.size(); ++tId) record->updateThreadString(tId);
    record->sortStrings();

    // Build the marker categories items
    record->buildMarkerCategories();

    return record;
}
