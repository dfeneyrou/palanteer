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

// This file implements the recording functionality (live or import) and saves the record file.

// System
#include <cstring>
#include <cstdio>
#include <chrono>
#include <algorithm>

// Internal
#include "bsOs.h"
#include "bsVec.h"
#include "cmInterface.h"
#include "cmRecording.h"
#include "cmCompress.h"

#ifndef PL_GROUP_REC
#define PL_GROUP_REC 0
#endif


cmRecording::cmRecording(cmInterface* itf, const bsString& storagePath, bool doForwardEvents) :
    _itf(itf), _doForwardEvents(doForwardEvents), _storagePath(storagePath), _doStopThread(0),
    _recMemAllocLkup(4096), _recElemPathToId(32768), _recMStreamStringHashLkup(32768)
{
    static_assert((cmChunkSize%cmMRScopeSize)==0, "Chunk size must be a multiple of the MR scope size");
    static_assert((cmElemChunkSize%cmMRElemSize)==0, "Elem chunk size must be a multiple of MR elem size");
    static_assert(sizeof(cmRecord::Evt)==32, "Unexpected size of cmRecord::Evt");
    static_assert(sizeof(u32)*cmElemChunkSize==sizeof(cmRecord::Evt)*cmChunkSize, "Sizes do not match"); // In theory we could remove this constraint (not in practice at the moment)

    // Ensure that the path has a '/' at its end
    if(!_storagePath.empty() && _storagePath.back()!=PL_DIR_SEP_CHAR) _storagePath.push_back(PL_DIR_SEP_CHAR);

    // Compression context
#if defined(PL_NO_COMPRESSION)
    _isCompressionEnabled = false;
#else
    _isCompressionEnabled = true;
#endif

    // Some reserve
    _recMarkerCategoryNameIdxs.reserve(256);
    _recStreams.reserve(cmConst::MAX_STREAM_QTY);
    _recLocks.reserve(256);
    _recElems.reserve(512);
    _recThreads.reserve(cmConst::MAX_THREAD_QTY);
    _recStrings.reserve(1024);
    _workingCompressionBuffer.resize(sizeof(cmRecord::Evt)*cmChunkSize*2); // Enough for chunks, and will be resized if needed for memory snapshots
    _workingNewMRScopes.reserve(cmChunkSize);
    _workingNewMRElems.reserve(cmChunkSize);
    _workingNewMRElemValues.reserve(cmChunkSize);

    // Misc
    _recShortDateState.doResync = false;  // CPU & context switch short dates are not resynchronized because verbose and with big latency...
}


cmRecording::~cmRecording(void)
{
}


// ==============================================================================================
// Record management layer
// ==============================================================================================

cmRecord*
cmRecording::beginRecord(const bsString& appName, const cmStreamInfo& infos, s64 timeTickOrigin, double tickToNs,
                         bool isMultiStream, int cacheMBytes, const bsString& recordFilename,
                         bool doCreateLiveRecord, bsString& errorMsg)
{
    plScope("beginRecord");
    errorMsg.clear();
    _recordAppName.clear();
    _recordPath.clear();

    // Is record storage enabled? (it may not be, in live scripting case)
    if(!recordFilename.empty()) {
        _recordAppName = appName;
        _recordPath    = recordFilename;

        // Open the record file
        _recFd = osFileOpen(_recordPath, "wb"); // Write only
        if(!_recFd) {
            plMarker("Error", "unable to open the record file for writing");
            errorMsg = bsString("Unable to open the record file ")+_recordPath+" for writing.\nPlease check the write permissions and existence of directories";
            return 0;
        }
    }

    // Store the fields
    _recTimeTickOrigin = timeTickOrigin;
    _recTickToNs       = tickToNs;
    _isMultiStream     = isMultiStream;

    // Short date, short string hash and hash salt are global to the record, not per stream
    _isDateShort       = infos.tlvs[PL_TLV_HAS_SHORT_DATE];
    _hashEmptyString   = (infos.tlvs[PL_TLV_HAS_SHORT_STRING_HASH]? BS_FNV_HASH32_OFFSET : BS_FNV_HASH_OFFSET) +
        infos.tlvs[PL_TLV_HAS_HASH_SALT];  // Value is 0 if no salt is added

    // Reset the structured storage
    _recDurationNs          = 0;
    _recLastEventFileOffset = 0;
    _recShortDateState.reset();
    _recCoreQty        = 0;
    _recUsedCoreCount  = 0;
    _recElemChunkQty   = 0;
    _recElemEventQty   = 0;
    _recMemEventQty    = 0;
    _recLockEventQty   = 0;
    _recMarkerEventQty   = 0;
    _recCtxSwitchEventQty = 0;
    _recLastIdxErrorQty = 0;
    _recErrorQty        = 0;
    memset(_recCoreIsUsed,   0, sizeof(_recCoreIsUsed));
    _recMemAllocLkup.clear();
    _recElemPathToId.clear();
    _recMarkerCategoryNameIdxs.clear();
    _recStreams.clear();
    _recStreams.push_back(infos);
    _recLocks.clear();
    _recElems.clear();
    _recThreads.clear();
    LOC_STORAGE_RESET(_recGlobal.lockUse);
    LOC_STORAGE_RESET(_recGlobal.lockNtf);
    LOC_STORAGE_RESET(_recGlobal.coreUsage);
    LOC_STORAGE_RESET(_recGlobal.marker);
    _recStrings.clear();
    _recErrorLkup.clear();

    _recMStreamStringHashLkup.clear();
    for(int i=0; i<cmConst::MAX_STREAM_QTY; ++i) {
        _recMStreamStringIdLkup[i].clear();
        _recMStreamStringIdLkup[i].reserve(4096);
    }
    memset(_recMStreamThreadIdLkup, 0xFF, sizeof(_recMStreamThreadIdLkup));
    memset(_recMStreamCoreIdLkup  , 0xFF, sizeof(_recMStreamCoreIdLkup));
    _recMStreamCoreQty = 0;
    memset(_recMStreamLastCSwitchDateNs, 0, sizeof(_recMStreamLastCSwitchDateNs));

    _recLastSizeStrings = 0;
    _recNameUpdatedThreadIds.clear();
    _recUpdatedElemIds.clear();
    _recUpdatedLockIds.clear();
    _recUpdatedStringIds.clear();

    cmRecord* liveRecord = 0;
    if(doCreateLiveRecord) {
        plAssert(_recFd, "Having a live record implies recording is enabled. This is not the case.");

        // Build the empty live record. It will be incrementally updated in memory, but raw events come from the same 'events' file
        FILE* fdReadEventChunks  = osFileOpen(_recordPath, "rb"); // Read only
        if(!fdReadEventChunks) {
            plMarker("Error", "unable to open file 'events' for live reading");
            errorMsg = bsString("Unable to open the record file ")+_recordPath+" for live reading.";
            fclose(_recFd);
            return 0;
        }

        liveRecord = new cmRecord(fdReadEventChunks, cacheMBytes); // Ownership of fds transferred.
        liveRecord->appName    = _recordAppName;
        liveRecord->recordPath = _recordPath;
        liveRecord->recordDate = osGetCreationDate(_recordPath);
        liveRecord->compressionMode = _isCompressionEnabled? 1 : 0;
        liveRecord->isMultiStream   = _isMultiStream? 1 : 0;
        liveRecord->streams.push_back(infos);
        liveRecord->loadExternalStrings();
    }

    return liveRecord;
}


const bsString&
cmRecording::storeNewString(int streamId, const bsString& newString, u64 hash)
{
    plScope("storeNewString");

    // Multistream
    if(_isMultiStream) {
        // Update the stringId redirection depending if the hash is already known
        int* pStringIdx = _recMStreamStringHashLkup.find(hash, hash);
        if(pStringIdx) {
            // Store the previous known index for this string (received in order)
            _recMStreamStringIdLkup[streamId].push_back(*pStringIdx);
            // No need to store the new string as we reuse the previously stored one
            return _recStrings[*pStringIdx].value;
        } else {
            _recMStreamStringHashLkup.insert(hash, hash, _recStrings.size());
            // Store the top index for this string (received in order)
            _recMStreamStringIdLkup[streamId].push_back(_recStrings.size());
        }
    }

    // Store in memory
    u32 length = newString.size();
    _recStrings.push_back( { newString, "", hash, 0LL, 0, 1, -1, -1, (length==1), false } );
    plData("New string pushed ID", _recStrings.size()-1); // We cannot push the string content as "static", because its pointer is not persistent
    return _recStrings.back().value;
}


#define INSERT_IN_ELEM(elem_, elemIdx_, lIdx_, time_, value_, threadBitmap_) \
    (elem_).chunkLIdx  .push_back(lIdx_);                               \
    (elem_).chunkTimes .push_back(time_);                               \
    (elem_).chunkValues.push_back(value_);                              \
    (elem_).threadBitmap |= (threadBitmap_);                            \
    plAssert((elem_).chunkLIdx.size()<=cmElemChunkSize, (elem_).chunkLIdx.size(), cmElemChunkSize); \
    if((elem_).chunkLIdx.size()==cmElemChunkSize) writeElemChunk((elem_)); \
    if(!(elem_).hasDeltaChanges) { (elem_).hasDeltaChanges = true; _recUpdatedElemIds.push_back(elemIdx_); }


void
cmRecording::processMarkerEvent(plPriv::EventExt& evtx, ThreadBuild& tc, int level, bool doForwardEvents)
{
    plgScope(REC, "processMarkerEvent");
    // Store complete chunks
    if(_recGlobal.markerChunkData.size()==cmChunkSize) writeGenericChunk(_recGlobal.markerChunkData, _recGlobal.markerChunkLocs);
    _recGlobal.markerChunkData.push_back(cmRecord::Evt { {{PL_INVALID, PL_INVALID}}, evtx.nameIdx, { evtx.filenameIdx },
                                                         evtx.threadId, (u8)level, evtx.flags, 0, evtx.lineNbr, 0,
                                                         { (u64)evtx.vS64 } } );
    ++tc.markerEventQty;
    ++_recMarkerEventQty;
    u32 lIdx = _recGlobal.markerChunkLocs.size()*cmChunkSize+_recGlobal.markerChunkData.size()-1;

    // Update the list of global marker categories
    if(_recStrings[evtx.nameIdx].categoryId<0) {
        _recMarkerCategoryNameIdxs.push_back(evtx.nameIdx);
        cmRecord::String& s = _recStrings[evtx.nameIdx];
        s.categoryId = _recMarkerCategoryNameIdxs.size()-1; // Mark the string as a category name
        if(!s.isHexa) { // Used as a changed flag, only in this file
            s.isHexa = true;
            _recUpdatedStringIds.push_back(evtx.nameIdx);
        }
    }

    // Elem 1: Per thread storage, for proper MR per thread (triangles)
    u64  itemHashPath = bsHashStepChain(tc.threadHash, cmConst::MARKER_NAMEIDX);
    int* elemIdxPtr   = _recElemPathToId.find(itemHashPath, cmConst::MARKER_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, bsHashStep(cmConst::MARKER_NAMEIDX), 0, cmConst::MARKER_NAMEIDX, (u32)-1, evtx.threadId, -1,
                evtx.nameIdx, evtx.nameIdx, evtx.flags, false, false, true, 1., 1. } );
        _recElemPathToId.insert(itemHashPath, cmConst::MARKER_NAMEIDX, _recElems.size()-1);
    }
    int elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem  = _recElems[elemIdx];
    INSERT_IN_ELEM(elem, elemIdx, lIdx, evtx.vS64, 1., 1LL<<evtx.threadId);

    // Elem 2: Per thread and per nameIdx (=category), for plot & histogram
    u64 partialItemHashPath = bsHashStepChain(evtx.nameIdx, cmConst::MARKER_NAMEIDX);
    itemHashPath = bsHashStep(tc.threadHash, partialItemHashPath);
    elemIdxPtr   = _recElemPathToId.find(itemHashPath, cmConst::MARKER_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, partialItemHashPath, 0, cmConst::MARKER_NAMEIDX, (u32)-1, evtx.threadId, -1,
                evtx.nameIdx, evtx.nameIdx, evtx.flags, false, false, true } );
        _recElemPathToId.insert(itemHashPath, cmConst::MARKER_NAMEIDX, _recElems.size()-1);
        if(_doForwardEvents && doForwardEvents) _itf->notifyNewElem(_recStrings[evtx.nameIdx].hash, _recElems.size()-1, -1, evtx.threadId, evtx.flags);
    }
    elemIdx     = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem2 = _recElems[elemIdx];
    // Update the elem
    double value = evtx.filenameIdx;
    if(elem2.absYMin>value) elem2.absYMin = value;
    if(elem2.absYMax<value) elem2.absYMax = value;
    INSERT_IN_ELEM(elem2, elemIdx, lIdx, evtx.vS64, 1., 1LL<<evtx.threadId);
    if(_doForwardEvents && doForwardEvents) _itf->notifyFilteredEvent(elemIdx, evtx.flags, _recStrings[evtx.nameIdx].hash, evtx.vS64, evtx.filenameIdx);
}


void
cmRecording::createLock(int streamId, u32 nameIdx)
{
    // Mark the string as a lock name
    _recStrings[nameIdx].lockId = _recLocks.size();

    // Create the lock
    _recLocks.push_back( {nameIdx} );

    // Multistream initialization (to ensure unicity of the lock name globally)
    if(_isMultiStream) {
        memset(&_recLocks.back().mStreamNameLkup[0], 0xFF, sizeof(LockBuild::mStreamNameLkup));
        _recLocks.back().mStreamNameLkup[streamId] = nameIdx; // Original lock name points on itself
    }
}



void
cmRecording::processLockNotifyEvent(plPriv::EventExt& evtx, ThreadBuild& tc, int level, bool doForwardEvents)
{
    plgScope(REC, "processLockNotifyEvent");

    // Create the lock
    if(_recStrings[evtx.nameIdx].lockId<0) {
        createLock(tc.streamId, evtx.nameIdx);
    }

    // Store complete chunks
    if(_recGlobal.lockNtfChunkData.size()==cmChunkSize) writeGenericChunk(_recGlobal.lockNtfChunkData, _recGlobal.lockNtfChunkLocs);
    _recGlobal.lockNtfChunkData.push_back(cmRecord::Evt { {{PL_INVALID, PL_INVALID}}, evtx.nameIdx, { evtx.filenameIdx },
                                                          evtx.threadId, (u8)level, evtx.flags, 0, evtx.lineNbr, 0,
                                                          { (u64)evtx.vS64 } } );
    ++tc.lockEventQty;
    ++_recLockEventQty;
    u32 lIdx = _recGlobal.lockNtfChunkLocs.size()*cmChunkSize+_recGlobal.lockNtfChunkData.size()-1;

    // Elem 1: Per thread storage, for proper MR per thread (triangles)    @#TBC Does not seem used...
    u64 itemHashPath = bsHashStepChain(tc.threadHash, cmConst::LOCK_NTF_NAMEIDX);
    int* elemIdxPtr  = _recElemPathToId.find(itemHashPath, cmConst::LOCK_NTF_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, bsHashStep(cmConst::LOCK_NTF_NAMEIDX), 0, cmConst::LOCK_NTF_NAMEIDX, (u32)-1, evtx.threadId, -1,
                evtx.nameIdx, evtx.nameIdx, evtx.flags, false, false, true, 0., cmConst::MAX_THREAD_QTY } );
        _recElemPathToId.insert(itemHashPath, cmConst::LOCK_NTF_NAMEIDX, _recElems.size()-1);
    }
    int elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem = _recElems[elemIdx];
    INSERT_IN_ELEM(elem, elemIdx, lIdx, evtx.vS64, 1., 1LL<<evtx.threadId);

    // Elem 2: Per lock nameIdx, for scripts and lock timeline
    itemHashPath = bsHashStepChain(_recStrings[evtx.nameIdx].hash, cmConst::LOCK_NTF_NAMEIDX);
    elemIdxPtr   = _recElemPathToId.find(itemHashPath, cmConst::LOCK_NTF_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, itemHashPath, 0, cmConst::LOCK_NTF_NAMEIDX, (u32)-1, -1, -1,
                evtx.nameIdx, evtx.nameIdx, evtx.flags, false, false, false, 1., 1. } );
        _recElemPathToId.insert(itemHashPath, cmConst::LOCK_NTF_NAMEIDX, _recElems.size()-1);
        if(_doForwardEvents && doForwardEvents) _itf->notifyNewElem(_recStrings[evtx.nameIdx].hash, _recElems.size()-1, -1, evtx.threadId, evtx.flags);
    }
    elemIdx     = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem2 = _recElems[elemIdx];
    INSERT_IN_ELEM(elem2, elemIdx, lIdx, evtx.vS64, 1., 1LL<<evtx.threadId);
    if(_doForwardEvents && doForwardEvents) _itf->notifyFilteredEvent(elemIdx, evtx.flags, _recStrings[evtx.nameIdx].hash, evtx.vS64, 0);
}


void
cmRecording::processLockWaitEvent(plPriv::EventExt& evtx, ThreadBuild& tc, int level)
{
    plgScope(REC, "processLockWaitEvent");

    // Store complete chunks
    if(tc.lockWaitChunkData.size()==cmChunkSize) writeGenericChunk(tc.lockWaitChunkData, tc.lockWaitChunkLocs);
    tc.lockWaitChunkData.push_back(cmRecord::Evt { {{PL_INVALID, PL_INVALID}}, evtx.nameIdx, {evtx.filenameIdx},
                                                   evtx.threadId, (u8)level, evtx.flags, 0, evtx.lineNbr, 0,
                                                   { (u64)evtx.vS64 } } );
    ++_recLockEventQty;
    ++tc.lockEventQty;

    // Update the list of unique waited lock names in this thread
    if(evtx.flags&PL_FLAG_SCOPE_BEGIN) { // Halves the work for identical result
        bool isNameAlreadyPresent = false;
        for(u32 lockNameIdx : tc.lockWaitNameIdxs) {
            if(lockNameIdx==evtx.nameIdx) { isNameAlreadyPresent = true; break; }
        }
        if(!isNameAlreadyPresent) {
            // Mark the lock as "waited by this thread"
            tc.lockWaitNameIdxs.push_back(evtx.nameIdx);
            // Create the lock
            cmRecord::String& s = _recStrings[evtx.nameIdx];
            if(s.lockId<0) {
                createLock(tc.streamId, evtx.nameIdx);
            }
            _recLocks[s.lockId].waitingThreadIds.push_back(evtx.threadId);
            // Live update
            _recUpdatedLockIds.push_back(s.lockId);
            if(!s.isHexa) { // Used as a changed flag, only in this file
                s.isHexa = true;
                _recUpdatedStringIds.push_back(evtx.nameIdx);
            }
        }
    }
    tc.lockWaitCurrentlyWaiting = (evtx.flags&PL_FLAG_SCOPE_BEGIN); // Keep the last state
    if(tc.lockWaitCurrentlyWaiting) {
        tc.lockWaitBeginTimeNs = evtx.vS64;
    }

    // Get the elem from the path hash
    u64  itemHashPath = bsHashStepChain(tc.threadHash, cmConst::LOCK_WAIT_NAMEIDX);
    int* elemIdxPtr   = _recElemPathToId.find(itemHashPath, cmConst::LOCK_WAIT_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, bsHashStep(cmConst::LOCK_WAIT_NAMEIDX), 0, cmConst::LOCK_WAIT_NAMEIDX, (u32)-1, evtx.threadId, 0,
                evtx.nameIdx, evtx.nameIdx, (evtx.flags&PL_FLAG_TYPE_MASK) | PL_FLAG_SCOPE_BEGIN, true, false, true } );
        _recElemPathToId.insert(itemHashPath, cmConst::LOCK_WAIT_NAMEIDX, _recElems.size()-1);
    }
    int elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem  = _recElems[elemIdx];
    double value = (double)(evtx.vS64-tc.lockWaitBeginTimeNs);  // 0 for "begin", the duration for "end"
    if(elem.absYMin>value) elem.absYMin = value;
    if(elem.absYMax<value) elem.absYMax = value;
    INSERT_IN_ELEM(elem, elemIdx, tc.lockWaitChunkLocs.size()*cmChunkSize+tc.lockWaitChunkData.size()-1, evtx.vS64, (evtx.flags&PL_FLAG_SCOPE_BEGIN)? 1.:0., 1LL<<evtx.threadId);
}


bool
cmRecording::processLockUseEvent(int streamId, plPriv::EventExt& evtx, bool& doInsertLockWaitEnd)
{
    plgScope(REC, "processLockUseEvent");

    // Get the elem from the path hash
    u64  itemHashPath = bsHashStepChain(_recStrings[evtx.nameIdx].hash, cmConst::LOCK_USE_NAMEIDX);
    int* elemIdxPtr   = _recElemPathToId.find(itemHashPath, cmConst::LOCK_USE_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, itemHashPath, 0, cmConst::LOCK_USE_NAMEIDX, (u32)-1, -1, -1,
                evtx.nameIdx, evtx.nameIdx, PL_FLAG_TYPE_LOCK_ACQUIRED, true, false, false } );
        _recElemPathToId.insert(itemHashPath, cmConst::LOCK_USE_NAMEIDX, _recElems.size()-1);
        if(_doForwardEvents) _itf->notifyNewElem(_recStrings[evtx.nameIdx].hash, _recElems.size()-1, -1, evtx.threadId, PL_FLAG_TYPE_LOCK_ACQUIRED);
        // Create the lock
        if(_recStrings[evtx.nameIdx].lockId<0) {
            createLock(streamId, evtx.nameIdx);
        }
    }
    int elemIdx     = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem = _recElems[elemIdx];

    // Shall we generate a "wait end" event from this lock use event?
    doInsertLockWaitEnd = (evtx.threadId<_recThreads.size() && _recThreads[evtx.threadId].lockWaitCurrentlyWaiting);

    // De-duplicate values by checking against the stored state
    LockBuild& lock = _recLocks[_recStrings[evtx.nameIdx].lockId];
    if(lock.isInUse) {
        if(evtx.flags==PL_FLAG_TYPE_LOCK_ACQUIRED) return false; // Duplicated, already in use
    }
    else if(evtx.flags==PL_FLAG_TYPE_LOCK_RELEASED) return false; // Duplicated, already not in use
    lock.isInUse = !lock.isInUse; // Toggle the use state

    plAssert(_recStrings[evtx.nameIdx].lockId>=0, "By design, each lock should have a valid entry");
    ++_recLockEventQty;

    // Store complete chunks
    if(_recGlobal.lockUseChunkData.size()==cmChunkSize) writeGenericChunk(_recGlobal.lockUseChunkData, _recGlobal.lockUseChunkLocs);
    _recGlobal.lockUseChunkData.push_back(cmRecord::Evt { {{PL_INVALID, PL_INVALID}}, evtx.nameIdx, {evtx.filenameIdx},
                                                          evtx.threadId, 0, evtx.flags, 0, evtx.lineNbr, 0,
                                                          { (u64)evtx.vS64 } } );

    if(lock.isInUse) {
        // Lock is acquired, store the information
        lock.usingStartThreadId = evtx.threadId;
        lock.usingStartTimeNs   = evtx.vS64;
    }

    // Update the elem
    u32 lIdx = _recGlobal.lockUseChunkLocs.size()*cmChunkSize+_recGlobal.lockUseChunkData.size()-1;
    if(!lock.isInUse) {
        // The lock duration is known when it is released
        double value = (double)(evtx.vS64-lock.usingStartTimeNs);
        if(elem.absYMin>value) elem.absYMin = value;
        if(elem.absYMax<value) elem.absYMax = value;
    }
    INSERT_IN_ELEM(elem, elemIdx, lIdx, evtx.vS64, (evtx.flags==PL_FLAG_TYPE_LOCK_ACQUIRED)? 1.:0., 1LL<<evtx.threadId);

    // Elem 2: Per thread and per nameIdx, for plot & histogram
    int threadId = lock.usingStartThreadId;
    u64 partialItemHashPath = bsHashStepChain(_recStrings[evtx.nameIdx].hash, cmConst::LOCK_USE_NAMEIDX);
    itemHashPath = bsHashStep(_recThreads[evtx.threadId].threadHash, partialItemHashPath);
    elemIdxPtr   = _recElemPathToId.find(itemHashPath, cmConst::LOCK_USE_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, partialItemHashPath, 0, cmConst::LOCK_USE_NAMEIDX, (u32)-1, threadId, -1,
                evtx.nameIdx, evtx.nameIdx, PL_FLAG_TYPE_LOCK_ACQUIRED, false, false, true } );
        _recElemPathToId.insert(itemHashPath, cmConst::LOCK_USE_NAMEIDX, _recElems.size()-1);
        if(_doForwardEvents) _itf->notifyNewElem(_recStrings[evtx.nameIdx].hash, _recElems.size()-1, -1, threadId, PL_FLAG_TYPE_LOCK_ACQUIRED);
    }
    elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem2 = _recElems[elemIdx];
    // Update the elem
    if(lock.isInUse) {
        // Storing "acquired" lock
        INSERT_IN_ELEM(elem2, elemIdx, lIdx, evtx.vS64, 0, 1LL<<evtx.threadId);
    }
    else {
        // The lock duration is known when it is released
        double value = (double)(evtx.vS64-lock.usingStartTimeNs);
        if(elem2.absYMin>value) elem2.absYMin = value;
        if(elem2.absYMax<value) elem2.absYMax = value;
        INSERT_IN_ELEM(elem2, elemIdx, lIdx, lock.usingStartTimeNs, value, 0);
        if(_doForwardEvents) _itf->notifyFilteredEvent(elemIdx, PL_FLAG_TYPE_LOCK_ACQUIRED, _recStrings[evtx.nameIdx].hash, lock.usingStartTimeNs, (u64)value);
    }

    return true;
}


void
cmRecording::processCtxSwitchEvent(plPriv::EventExt& evtx, ThreadBuild& tc)
{
    plgScope(REC, "processCtxSwitchEvent");
    // Store complete chunks
    if(tc.ctxSwitchChunkData.size()==cmChunkSize) writeGenericChunk(tc.ctxSwitchChunkData, tc.ctxSwitchChunkLocs);
    tc.ctxSwitchChunkData.push_back(cmRecord::Evt { {{PL_INVALID, PL_INVALID}}, evtx.nameIdx, {evtx.newCoreId},
                                                    evtx.threadId, 0, evtx.flags, 0, 0, 0,
                                                    { (u64)evtx.vS64 } } );
    ++_recCtxSwitchEventQty;
    ++tc.ctxSwitchEventQty;
    // Get the elem from the path hash   @#SIMPLIFY The assumption about mandatory thread declaration below is no more true. We can use the threadHash as for all other cases. Iterator shall be updated too
    // Note that we use the "threadId" and not its hash name here, because no need for persistency across run for any config (none existing)
    //  and also ctx switch events would be dropped at the beginning of the record because they are sent before the thread declaration due to the
    //  double buffering mechanism in the client side (ctx switch events bypass this double buffering on some OS (Linux...))
    u64 itemHashPath = bsHashStepChain(evtx.threadId, cmConst::CTX_SWITCH_NAMEIDX);
    int* elemIdxPtr  = _recElemPathToId.find(itemHashPath, cmConst::CTX_SWITCH_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, itemHashPath, 0, cmConst::CTX_SWITCH_NAMEIDX, (u32)-1, evtx.threadId, 0,
                PL_INVALID, PL_INVALID, (evtx.flags&PL_FLAG_TYPE_MASK) | PL_FLAG_SCOPE_BEGIN, true, false, false } );
        _recElemPathToId.insert(itemHashPath, cmConst::CTX_SWITCH_NAMEIDX, _recElems.size()-1);
    }

    // Update the elem
    int elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem = _recElems[elemIdx];
    // No core=-1 (so that our "max" filtering favorises core usage)
    // @#TBC Unique case with doRepresentZon.=true and a desired "max" behavior (favorising high values). Not really unique, same with CoreUsage bars
    INSERT_IN_ELEM(elem, elemIdx, tc.ctxSwitchChunkLocs.size()*cmChunkSize+tc.ctxSwitchChunkData.size()-1, evtx.vS64, (s8)evtx.newCoreId, 1LL<<evtx.threadId);
}


void
cmRecording::processSoftIrqEvent(plPriv::EventExt& evtx, ThreadBuild& tc)
{
    // Sanity
    if(evtx.threadId>=cmConst::MAX_THREAD_QTY) return;
    plgScope(REC, "processSoftIrqEvent");

    tc.isSoftIrqScopeOpen = (evtx.flags&PL_FLAG_SCOPE_BEGIN);

    // Store complete chunks
    if(tc.softIrqChunkData.size()==cmChunkSize) writeGenericChunk(tc.softIrqChunkData, tc.softIrqChunkLocs);
    tc.softIrqChunkData.push_back(cmRecord::Evt { {{PL_INVALID, PL_INVALID}}, evtx.nameIdx, {evtx.newCoreId},
                                                  evtx.threadId, 0, evtx.flags, 0, 0, 0,
                                                  { (u64)evtx.vS64 } } );
    ++_recCtxSwitchEventQty;

    // Get the elem from the path hash
    u64 itemHashPath = bsHashStepChain(evtx.threadId,      cmConst::SOFTIRQ_NAMEIDX);
    int* elemIdxPtr  = _recElemPathToId.find(itemHashPath, cmConst::SOFTIRQ_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, itemHashPath, 0, cmConst::SOFTIRQ_NAMEIDX, (u32)-1, -1, 0,
                PL_INVALID, PL_INVALID, (evtx.flags&PL_FLAG_TYPE_MASK) | PL_FLAG_SCOPE_BEGIN, true, false, false } );
        _recElemPathToId.insert(itemHashPath, cmConst::SOFTIRQ_NAMEIDX, _recElems.size()-1);
    }

    // Update the elem
    int elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem = _recElems[elemIdx];
    INSERT_IN_ELEM(elem, elemIdx, tc.softIrqChunkLocs.size()*cmChunkSize+tc.softIrqChunkData.size()-1, evtx.vS64, (evtx.flags&PL_FLAG_SCOPE_BEGIN)? 1.:0., 1LL<<evtx.threadId);
}


bool
cmRecording::processCoreUsageEvent(int streamId, plPriv::EventExt& evtx)
{
    // Get the coreId
    int coreId = (evtx.newCoreId==0xFF)? evtx.prevCoreId : evtx.newCoreId;
    if(coreId==0xFF) return false; // Weird as no core is identified in this event. Client issue?
    plgScope(REC, "processCoreUsageEvent");

    // Update the date and ensure that the clock is always increasing for context switches.
    // Indeed, clock resynchronization may add some jitter and make some dates backward at these points.
    // This would damage the speck computation for multi-resolution
    updateDate(evtx, _recShortDateState);
    if(evtx.vS64<_recMStreamLastCSwitchDateNs[streamId]) evtx.vS64 = _recMStreamLastCSwitchDateNs[streamId]+1;
    _recMStreamLastCSwitchDateNs[streamId] = evtx.vS64;

    // Check that the CPU usage by our program changed
    bool doAddCpuPoint = !_isMultiStream;  // Cannot be achieved simply in multistream without an aggregator on streams. Maybe later.
    while(coreId>=_recCoreQty) _recCoreIsUsed[_recCoreQty++] = 0;
    if(evtx.newCoreId==0xFF && _recCoreIsUsed[coreId]!=0) { // Our program does not use anymore this core
        _recCoreIsUsed[coreId] = 0;
        --_recUsedCoreCount;
    }
    else if(evtx.newCoreId!=0xFF && evtx.threadId!=0xFF && _recCoreIsUsed[coreId]==0) { // Our program starts to use this core
        _recCoreIsUsed[coreId] = 1;
        ++_recUsedCoreCount;
    }
    else doAddCpuPoint = false; // No change = need to update the CPU curve

    // Store complete chunks
    if(_recGlobal.coreUsageChunkData.size()==cmChunkSize) writeGenericChunk(_recGlobal.coreUsageChunkData, _recGlobal.coreUsageChunkLocs);
    _recGlobal.coreUsageChunkData.push_back(cmRecord::Evt { {{(u32)_recUsedCoreCount, PL_INVALID}}, evtx.nameIdx, {evtx.newCoreId},
                                                            evtx.threadId, 0, PL_FLAG_TYPE_CSWITCH, 0, 0, 0,
                                                            { (u64)evtx.vS64 } } );
    ++_recCtxSwitchEventQty;

    // Get the elem for this core
    u64  itemHashPath = bsHashStepChain(coreId, cmConst::CORE_USAGE_NAMEIDX);
    int* elemIdxPtr   = _recElemPathToId.find(itemHashPath, cmConst::CORE_USAGE_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { itemHashPath, itemHashPath, 0, cmConst::CORE_USAGE_NAMEIDX, (u32)-1, coreId, 0,
                PL_INVALID, PL_INVALID, PL_FLAG_TYPE_CSWITCH, true, false, false } );
        _recElemPathToId.insert(itemHashPath, cmConst::CORE_USAGE_NAMEIDX, _recElems.size()-1);
    }

    // Update the elem for this core
    int elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem = _recElems[elemIdx];
    // Value is either -1 (not used) or the newCoreId (=used)
    // @#TBC Cf comment from context switch on the potential MR config problem here
    INSERT_IN_ELEM(elem, elemIdx, _recGlobal.coreUsageChunkLocs.size()*cmChunkSize+_recGlobal.coreUsageChunkData.size()-1, evtx.vS64, (evtx.newCoreId==0xFF)? -1 : evtx.newCoreId, 0);

    if(doAddCpuPoint) {
        // Get the Elem for the CPU curve
        itemHashPath = bsHashStepChain(cmConst::CPU_CURVE_NAMEIDX);
        elemIdxPtr   = _recElemPathToId.find(itemHashPath, cmConst::CPU_CURVE_NAMEIDX);
        if(!elemIdxPtr) {
            _recElems.push_back( { itemHashPath, itemHashPath, 0, cmConst::CPU_CURVE_NAMEIDX, (u32)-1, -1, 0,
                    PL_INVALID, PL_INVALID, PL_FLAG_TYPE_CSWITCH, false, false, false } );
            _recElemPathToId.insert(itemHashPath, cmConst::CPU_CURVE_NAMEIDX, _recElems.size()-1);
        }

        // Update the elem for this core
        elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
        ElemBuild& elem2 = _recElems[elemIdx];
        INSERT_IN_ELEM(elem2, elemIdx, _recGlobal.coreUsageChunkLocs.size()*cmChunkSize+_recGlobal.coreUsageChunkData.size()-1, evtx.vS64, _recUsedCoreCount, 0);
    }

    return (evtx.threadId!=PL_CSWITCH_CORE_NONE);
}


void
cmRecording::processMemoryEvent(plPriv::EventExt& evtx, ThreadBuild& tc, int level)
{
    plgScope(REC, "processMemoryEvent");
    NestingLevelBuild& lc = tc.levels[level];
    int eType = evtx.flags&PL_FLAG_TYPE_MASK;

    u32 allocQtyElemId = (u32)-1;
    int allocThreadId  = -1;
    u64 allocQtyValue  = 0;
    u32 allocMIdx      = 0;
    ThreadBuild* tcAlloc = 0; // We store deallocation on the allocating thread, so we need this indirection

    // Memory events "Part 1": pointers and sizes
    if     (eType==PL_FLAG_TYPE_ALLOC_PART  ) { lc.lastAllocPtr   = evtx.vU64; lc.lastAllocSize = evtx.memSize; }
    else if(eType==PL_FLAG_TYPE_DEALLOC_PART) { lc.lastDeallocPtr = evtx.vU64; } // We store in "lc", but will use the allocation thread in the second processing phase

    // Memory events "Part 2": process the (completed) memory event
    else if(eType==PL_FLAG_TYPE_ALLOC && lc.lastAllocPtr) {

        tcAlloc   = &tc;
        allocMIdx = tc.memAllocChunkLocs.size()*cmChunkSize+tc.memAllocChunkData.size();

        // Snapshot time? (counter can go negative, as we do it only on allocation event to ease iterator job later)
        // We do it before adding the current event so that a snapshot at 'allocMIdx' is all allocs before this index
        //  which is the required behavior for the memory iterator
        if((--tcAlloc->memEventQtyBeforeSnapshot)<=0) {
            saveThreadMemorySnapshot(*tcAlloc, evtx.vS64, allocMIdx);
        }

        // Update the list of currently allocated scopes
        int currentScopeIdx = 0;
        while(!tc.memSSEmptyIdx.empty() && tc.memSSEmptyIdx.back()>=tc.memSSCurrentAlloc.size()) tc.memSSEmptyIdx.pop_back(); // Cleaning after shrinking the content array
        if(tc.memSSEmptyIdx.empty()) {
            currentScopeIdx = tc.memSSCurrentAlloc.size();
            tc.memSSCurrentAlloc.resize(currentScopeIdx+1);
        } else {
            currentScopeIdx = tc.memSSEmptyIdx.back();
            tc.memSSEmptyIdx.pop_back();
            plAssert(tc.memSSCurrentAlloc[currentScopeIdx]==PL_INVALID);
        }
        tc.memSSCurrentAlloc[currentScopeIdx] = allocMIdx;

        // Store the virtual pointer, the mIndex of the alloc and its size, to associate it later with the dealloc event
        _recMemAllocLkup.insert(lc.lastAllocPtr, { evtx.threadId, lc.lastAllocSize, allocMIdx, currentScopeIdx } );

        // Update stats
        _recMemEventQty += 2;
        tc.memEventQty  += 2;
        tc.sumAllocQty  += 1;
        tc.sumAllocSize += lc.lastAllocSize;
        lc.lastAllocPtr = 0;
        allocQtyElemId = cmConst::MEMORY_ALLOCQTY_NAMEIDX;
        allocQtyValue  = tc.sumAllocQty;
        allocThreadId  = evtx.threadId;

        // Complete the previous memory event with a link to this one
        if(tc.lastIsAlloc) { if(!tc.memAllocChunkData.empty())   { tc.memAllocChunkData.back().memLinkIdx   = allocMIdx; } }
        else               { if(!tc.memDeallocChunkData.empty()) { tc.memDeallocChunkData.back().memLinkIdx = allocMIdx; } }
        tc.lastIsAlloc = true;

        // Store the new "alloc event" in the thread
        if(tc.memAllocChunkData.size()==cmChunkSize) writeGenericChunk(tc.memAllocChunkData, tc.memAllocChunkLocs);
        tc.memAllocChunkData.push_back(cmRecord::Evt{ {{PL_INVALID, lc.lastAllocSize}}, evtx.nameIdx, {tc.levels[level].parentNameIdx},
                                                      evtx.threadId, (u8)level, evtx.flags, 0, evtx.lineNbr, 0,
                                                      { evtx.vU64 } });
        tc.memDeallocMIdx.push_back(PL_INVALID); // If not leaked, will be overwritten when deallocated

        // Store the new "alloc call" elem (plottable)
        if(tc.memPlotChunkData.size()==cmChunkSize) writeGenericChunk(tc.memPlotChunkData, tc.memPlotChunkLocs);
        tc.memPlotChunkData.push_back(cmRecord::Evt{ {{0, 0}}, tc.levels[level].parentNameIdx, {0},
                                                     evtx.threadId, (u8)(level-1), tc.levels[level].parentFlags, 0, evtx.lineNbr, 0,
                                                     { evtx.vU64 } });
        tc.memPlotChunkData.back().memElemValue = tc.sumAllocQty;
    }

    else if(eType==PL_FLAG_TYPE_DEALLOC && lc.lastDeallocPtr) {
        // Find information about allocation
        VMemAlloc* ptr = _recMemAllocLkup.find(lc.lastDeallocPtr);
        if(ptr) {  // Known allocation? Should be... else we ignore it
            VMemAlloc allocElems = *ptr;

            // Remove it from active allocations
            allocThreadId = allocElems.threadId;
            tcAlloc       = &_recThreads[allocThreadId];
            bool isFound  = _recMemAllocLkup.erase(lc.lastDeallocPtr);
            plAssert(isFound);

            // Update the list of currently allocated scopes
            tcAlloc->memSSEmptyIdx.push_back(allocElems.currentScopeIdx);
            tcAlloc->memSSCurrentAlloc[allocElems.currentScopeIdx] = PL_INVALID;
            while(!tcAlloc->memSSCurrentAlloc.empty() && tcAlloc->memSSCurrentAlloc.back()==PL_INVALID) tcAlloc->memSSCurrentAlloc.pop_back(); // Shrink the content array if possible

            // Update stats
            _recMemEventQty         += 2;
            tcAlloc->memEventQty    += 2;
            tcAlloc->sumDeallocQty  += 1;
            tcAlloc->sumDeallocSize += allocElems.size;
            allocQtyElemId  = cmConst::MEMORY_DEALLOCQTY_NAMEIDX;
            allocQtyValue   = tcAlloc->sumDeallocQty;
            u32 deallocMIdx = tcAlloc->memDeallocChunkLocs.size()*cmChunkSize+tcAlloc->memDeallocChunkData.size();

            // Complete the previous memory event with a link to this one. Marked as dealloc thanks to 0x80000000
            if(tcAlloc->lastIsAlloc) { if(!tcAlloc->memAllocChunkData.empty())   { tcAlloc->memAllocChunkData.back()  .memLinkIdx = (deallocMIdx|0x80000000); } }
            else                     { if(!tcAlloc->memDeallocChunkData.empty()) { tcAlloc->memDeallocChunkData.back().memLinkIdx = (deallocMIdx|0x80000000); } }
            tcAlloc->lastIsAlloc = false;

            // Store the new "dealloc event"
            plAssert(allocElems.mIdx<(u32)tcAlloc->memDeallocMIdx.size());
            if(tcAlloc->memDeallocChunkData.size()==cmChunkSize) writeGenericChunk(tcAlloc->memDeallocChunkData, tcAlloc->memDeallocChunkLocs);
            tcAlloc->memDeallocMIdx[allocElems.mIdx] = deallocMIdx;
            tcAlloc->memDeallocChunkData.push_back(cmRecord::Evt { {{PL_INVALID, allocElems.mIdx}}, evtx.nameIdx, {tc.levels[level].parentNameIdx},
                                                                   evtx.threadId, (u8)level, evtx.flags, 0, evtx.lineNbr, 0,
                                                                   { evtx.vU64 } } );

            // Store the new "dealloc call" elem (plottable)
            if(tcAlloc->memPlotChunkData.size()==cmChunkSize) writeGenericChunk(tcAlloc->memPlotChunkData, tcAlloc->memPlotChunkLocs);
            tcAlloc->memPlotChunkData.push_back(cmRecord::Evt{ {{0, 0}}, tc.levels[level].parentNameIdx, {0},
                                                               evtx.threadId, (u8)(level-1), tc.levels[level].parentFlags, 0, evtx.lineNbr, 0,
                                                               { evtx.vU64 } });
            tcAlloc->memPlotChunkData.back().memElemValue = tcAlloc->sumDeallocQty;
        }
        lc.lastDeallocPtr = 0;
    }
    if(!tcAlloc) {
        return; // Nothing more to process (partial or invalid memory event)
    }
    plAssert(allocThreadId>=0);

    // Store the new "alloc size" elem (plottable) (common storage to both alloc and dealloc). Note that allocation thread is used here
    if(tcAlloc->memPlotChunkData.size()==cmChunkSize) writeGenericChunk(tcAlloc->memPlotChunkData, tcAlloc->memPlotChunkLocs);
    tcAlloc->memPlotChunkData.push_back(cmRecord::Evt{ {{0, 0}}, evtx.nameIdx, {tc.levels[level].parentNameIdx},
                                                       (u8)allocThreadId, (u8)(level-1), tc.levels[level].parentFlags, 0, evtx.lineNbr, 0,
                                                       { evtx.vU64 } });
    tcAlloc->memPlotChunkData.back().memElemValue = (s64)(_recThreads[allocThreadId].sumAllocSize-_recThreads[allocThreadId].sumDeallocSize);

    // Update the elem "allocSize" with the new element
    u64 sizeKindHashPath = bsHashStepChain(tcAlloc->threadHash, cmConst::MEMORY_ALLOCSIZE_NAMEIDX);
    int* elemIdxPtr      = _recElemPathToId.find(sizeKindHashPath, cmConst::MEMORY_ALLOCSIZE_NAMEIDX);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { sizeKindHashPath, bsHashStep(cmConst::MEMORY_ALLOCSIZE_NAMEIDX), 0, cmConst::MEMORY_ALLOCSIZE_NAMEIDX, (u32)-1, allocThreadId, 0,
                PL_INVALID, PL_INVALID, PL_FLAG_TYPE_ALLOC, false, false, true } );
        _recElemPathToId.insert(sizeKindHashPath, cmConst::MEMORY_ALLOCSIZE_NAMEIDX, _recElems.size()-1);
    }
    int elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem   = _recElems[elemIdx];
    double value = (double)tcAlloc->memPlotChunkData.back().memElemValue;
    if(elem.absYMin>value) elem.absYMin = value;
    if(elem.absYMax<value) elem.absYMax = value;
    INSERT_IN_ELEM(elem, elemIdx, tcAlloc->memPlotChunkLocs.size()*cmChunkSize+tcAlloc->memPlotChunkData.size()-1, evtx.vS64, value, 1LL<<evtx.threadId);

    // Update the elem "(de-)allocQty" with the new element
    u64 qtyKindHashPath = bsHashStepChain(tcAlloc->threadHash, allocQtyElemId);
    elemIdxPtr          = _recElemPathToId.find(qtyKindHashPath, allocQtyElemId);
    if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
        _recElems.push_back( { qtyKindHashPath, bsHashStep(allocQtyElemId), 0, allocQtyElemId, (u32)-1, allocThreadId, 0,
                PL_INVALID, PL_INVALID, PL_FLAG_TYPE_ALLOC, false, false, true } );
        _recElemPathToId.insert(qtyKindHashPath, allocQtyElemId, _recElems.size()-1);
    }
    elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
    ElemBuild& elem2 = _recElems[elemIdx];
    value = (double)allocQtyValue;
    if(elem2.absYMin>value) elem2.absYMin = value;
    if(elem2.absYMax<value) elem2.absYMax = value;
    // Memory stat index (before the "alloc size" one, hence the "-2")
    INSERT_IN_ELEM(elem2, elemIdx, tcAlloc->memPlotChunkLocs.size()*cmChunkSize+tcAlloc->memPlotChunkData.size()-2, evtx.vS64, value, 1LL<<evtx.threadId);
}


void
cmRecording::saveThreadMemorySnapshot(ThreadBuild& tc, s64 timeNs, u32 allocMIdx)
{
    plgScope(REC, "saveThreadMemorySnapshot");
    // Reset the event counter before next snapshot
    tc.memEventQtyBeforeSnapshot = PL_MEMORY_SNAPSHOT_EVENT_INTERVAL;
    if(!_recFd) return; // Case no recording on file

    // Write the current quantity of allocation
    u32 allocatedScopeQty = tc.memSSCurrentAlloc.size(); // It is rather an estimation of the qty as some PL_INVALID may be inside
    fwrite(&allocatedScopeQty, sizeof(u32), 1, _recFd);

    int writtenBufferSize = allocatedScopeQty*sizeof(u32);
    if(allocatedScopeQty) {
        if(_isCompressionEnabled) {
            plgScope(REC, "Compression");
            if(_workingCompressionBuffer.size()<writtenBufferSize*2) _workingCompressionBuffer.resize(writtenBufferSize*2);  // With some margin
            writtenBufferSize = _workingCompressionBuffer.size();  // Give some memory margin to the compression library (faster)
            cmCompressChunk((u8*)&tc.memSSCurrentAlloc[0], allocatedScopeQty*sizeof(u32), &_workingCompressionBuffer[0], &writtenBufferSize);
        }
        fwrite(_isCompressionEnabled? (void*)&_workingCompressionBuffer[0] : (void*)&tc.memSSCurrentAlloc[0], 1, writtenBufferSize, _recFd);
    }
    writtenBufferSize += sizeof(u32); // For the current quantity of allocation

    // Update the storage elems
    tc.memSnapshotIndexes.push_back( { timeNs, cmRecord::makeChunkLoc(_recLastEventFileOffset, writtenBufferSize), allocMIdx } );
    _recLastEventFileOffset += writtenBufferSize;
}


void
cmRecording::processScopeEvent(plPriv::EventExt& evtx, ThreadBuild& tc, int level)
{
    plgScope(REC, "processScopeEvent");
#define LOG_ERROR(type_)                                                \
    u64  errHash   = bsHashStepChain(evtx.threadId, ((evtx.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_MARKER)? evtx.filenameIdx : evtx.nameIdx, type_, evtx.lineNbr); \
    int* errIdxPtr = _recErrorLkup.find(errHash, type_);                \
    if(errIdxPtr) _recErrors[*errIdxPtr].count += 1;                    \
    else if(_recErrorQty<cmRecord::MAX_REC_ERROR_QTY) {                 \
        if((evtx.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_MARKER) {       \
            _recErrors[_recErrorQty++] = { type_, evtx.threadId, evtx.lineNbr, PL_INVALID, evtx.filenameIdx, 1 }; \
            if(_doForwardEvents) _itf->notifyInstrumentationError(type_, evtx.threadId, PL_INVALID, evtx.lineNbr, evtx.filenameIdx); \
        } else {                                                        \
            _recErrors[_recErrorQty++] = { type_, evtx.threadId, evtx.lineNbr, evtx.filenameIdx, evtx.nameIdx, 1 }; \
            if(_doForwardEvents) _itf->notifyInstrumentationError(type_, evtx.threadId, evtx.filenameIdx, evtx.lineNbr, evtx.nameIdx); \
        }                                                               \
        _recErrorLkup.insert(errHash, type_, _recErrorQty-1);           \
    }

    // Last linkLIdx shall point to its next item, except for "begin scope" which points to the first child in the next level
#define UPDATE_LINK(paramLc, paramLevel, paramCurrentLIdx, paramIsAScope) \
    if((paramLc).lastIsScope) {                                           \
        if(!(paramLc).scopeChunkData.empty() && !((paramLc).scopeChunkData.back().flags&PL_FLAG_SCOPE_BEGIN)) \
            (paramLc).scopeChunkData.back().linkLIdx = paramCurrentLIdx;  \
    } else {                                                            \
        if(!(paramLc).nonScopeChunkData.empty())                          \
            (paramLc).nonScopeChunkData.back().linkLIdx = paramCurrentLIdx; \
    }                                                                   \
    (paramLc).lastIsScope = paramIsAScope;                                \
    if((paramLevel)>0 && tc.levels[(paramLevel)-1].scopeChunkData.back().linkLIdx==PL_INVALID) { \
        plAssert(tc.levels[(paramLevel)-1].scopeChunkData.back().flags&PL_FLAG_SCOPE_BEGIN); \
        tc.levels[(paramLevel)-1].scopeChunkData.back().linkLIdx = paramCurrentLIdx; \
    }

    NestingLevelBuild& lc = tc.levels[level];
    int eType = evtx.flags&PL_FLAG_TYPE_MASK;

    // Inject the artificial memory events just before a "scope end".
    //   Typically used for memory based flame graphs and display nesting scope memory operations
    if((evtx.flags&PL_FLAG_SCOPE_BEGIN)) {
        // Snapshot the counters. This mechanism takes into account the full nested memory operations
        lc.beginSumAllocQty    = tc.sumAllocQty;
        lc.beginSumAllocSize   = tc.sumAllocSize;
        lc.beginSumDeallocQty  = tc.sumDeallocQty;
        lc.beginSumDeallocSize = tc.sumDeallocSize;
        // Mark the level as 'open scope' (required for pause management)
        lc.isScopeOpen = true;
    }
    if(evtx.flags&PL_FLAG_SCOPE_END) {
        // Mark the level as 'closed scope'
        lc.isScopeOpen = false;

        // Check that the name matches the "begin"
        plAssert(level+1<tc.levels.size(), level, tc.levels.size());
        if(evtx.nameIdx!=tc.levels[level+1].parentNameIdx) {
            // Empty string is a wildcard, so not an error
            if(_recStrings[evtx.nameIdx].hash!=_hashEmptyString) {
                LOG_ERROR(cmRecord::ERROR_MISMATCH_SCOPE_END);
            }
            // In any case, replace with the matching nameIdx
            evtx.nameIdx = tc.levels[level+1].parentNameIdx;
        }

        // Insert fake event to track inner memory allocations
        if(level+1<tc.levels.size()) {
            NestingLevelBuild& lcc = tc.levels[level+1];
            // Insert an alloc summary
            if(tc.sumAllocQty>lc.beginSumAllocQty) {
                u32 memCurrentLIdx = (lcc.nonScopeChunkLocs.size()*cmChunkSize+lcc.nonScopeChunkData.size()) | 0x80000000; // We create a non scope
                UPDATE_LINK(lcc, level+1, memCurrentLIdx, false);
                if(lcc.nonScopeChunkData.size()==cmChunkSize) writeGenericChunk(lcc.nonScopeChunkData, lcc.nonScopeChunkLocs);
                lcc.nonScopeChunkData.push_back(cmRecord::Evt { {{tc.levels[level].scopeCurrentLIdx, PL_INVALID}}, 0, {0},
                                                                evtx.threadId, (u8)(level+1), PL_FLAG_TYPE_ALLOC, 0, evtx.lineNbr, 0,
                                                                { ( ((tc.sumAllocQty-lc.beginSumAllocQty)<<32) | bsMin((u64)0xFFFFFFFFULL, tc.sumAllocSize-lc.beginSumAllocSize) ) } } );
                ++tc.elemEventQty;
                ++_recElemEventQty;
            }
            // Insert an dealloc summary
            if(tc.sumDeallocQty>lc.beginSumDeallocQty) {
                u32 memCurrentLIdx = (lcc.nonScopeChunkLocs.size()*cmChunkSize+lcc.nonScopeChunkData.size()) | 0x80000000; // We create a non scope
                UPDATE_LINK(lcc, level+1, memCurrentLIdx, false);
                if(lcc.nonScopeChunkData.size()==cmChunkSize) writeGenericChunk(lcc.nonScopeChunkData, lcc.nonScopeChunkLocs);
                lcc.nonScopeChunkData.push_back(cmRecord::Evt { {{tc.levels[level].scopeCurrentLIdx, PL_INVALID}}, 0, {0},
                                                                evtx.threadId, (u8)(level+1), PL_FLAG_TYPE_DEALLOC, 0, evtx.lineNbr, 0,
                                                                { ( ((tc.sumDeallocQty-lc.beginSumDeallocQty)<<32) | bsMin((u64)0xFFFFFFFFULL, tc.sumDeallocSize-lc.beginSumDeallocSize) ) } } );
                ++tc.elemEventQty;
                ++_recElemEventQty;
            }
        }
    }

    // Sanity check on the positive level
    bool doStoreInHierarchy = true;
    if(level==0 && !(evtx.flags&PL_FLAG_SCOPE_MASK)) {
        if(eType==PL_FLAG_TYPE_MARKER || eType==PL_FLAG_TYPE_LOCK_ACQUIRED || eType==PL_FLAG_TYPE_LOCK_RELEASED || eType==PL_FLAG_TYPE_LOCK_NOTIFIED) {
            doStoreInHierarchy = false;
        } else {
            LOG_ERROR(cmRecord::ERROR_EVENT_OUTSIDE_SCOPE);
            return;
        }
    }

    u64 evtThreadBitmap = (1LL<<evtx.threadId);
    if(doStoreInHierarchy) {
        // Get elems on the event
        bool isScope      = (evtx.flags&PL_FLAG_SCOPE_MASK);
        u32  currentLIdx = isScope?
            (lc.scopeChunkLocs.size()*cmChunkSize+lc.scopeChunkData.size()) :
            ((lc.nonScopeChunkLocs.size()*cmChunkSize+lc.nonScopeChunkData.size())|0x80000000); // Non-scopes have msb set
        ++tc.elemEventQty;
        ++_recElemEventQty;

        // Last linkLIdx shall point to next item, except for "begin scope" where it points to the first child in the next level
        //  and update parent link if needed (for "scope begin", linkLIdx is the potential child LIdx)
        UPDATE_LINK(lc, level, currentLIdx, isScope);

        // Store complete chunks
        if(lc.scopeChunkData   .size()==cmChunkSize) writeScopeChunk(lc);
        if(lc.nonScopeChunkData.size()==cmChunkSize) writeGenericChunk(lc.nonScopeChunkData, lc.nonScopeChunkLocs);

        // Store the current event data in a chunk (split in scope and non-scope)
        u32 parentIdx = (level>0)? tc.levels[level-1].scopeCurrentLIdx : PL_INVALID; // Always on scope data
        if(isScope) {
            // Store the "scope" event
            lc.scopeChunkData.push_back(cmRecord::Evt { {{parentIdx, PL_INVALID}}, evtx.nameIdx, {evtx.filenameIdx},
                                                        evtx.threadId, (u8)level, evtx.flags, 0, evtx.lineNbr, 0,
                                                        { evtx.vU64 } } );
            lc.scopeCurrentLIdx = currentLIdx;
        } else {
            // Store the "flat" event
            lc.nonScopeChunkData.push_back(cmRecord::Evt { {{parentIdx, PL_INVALID}}, evtx.nameIdx, {evtx.filenameIdx},
                                                           evtx.threadId, (u8)level, evtx.flags, 0, evtx.lineNbr, 0,
                                                           { evtx.vU64 } } );
        }

        // Get the elem from the path hash
        int hashFlags    = (evtx.flags&PL_FLAG_SCOPE_END)? ((evtx.flags&PL_FLAG_TYPE_MASK)|PL_FLAG_SCOPE_BEGIN) : evtx.flags; // Replace END scope with BEGIN scope (1 plot for both)
        u64 hashPath     = bsHashStep(_recStrings[evtx.nameIdx].hash, lc.hashPath);
        u64 partialItemHashPath = bsHashStep(hashFlags, hashPath);
        u64 itemHashPath = bsHashStep(tc.threadHash, partialItemHashPath);
        int* elemIdxPtr  = _recElemPathToId.find(itemHashPath, evtx.nameIdx);
        if(!elemIdxPtr) { // If this Elem does not exist yet, let's create it
            u32 hlNameIdx = ((evtx.flags&PL_FLAG_SCOPE_MASK)==0 && level>0)? tc.levels[level].parentNameIdx : evtx.nameIdx;
            _recElems.push_back( { itemHashPath, partialItemHashPath, 0, evtx.nameIdx, lc.prevElemIdx, evtx.threadId,
                    level, evtx.nameIdx, hlNameIdx, evtx.flags, false, true, true } );
            _recElemPathToId.insert(itemHashPath, evtx.nameIdx, _recElems.size()-1);
            if(_doForwardEvents) _itf->notifyNewElem(_recStrings[evtx.nameIdx].hash, _recElems.size()-1, lc.prevElemIdx, evtx.threadId, evtx.flags);
        }
        int elemIdx = elemIdxPtr? *elemIdxPtr:_recElems.size()-1;
        ElemBuild& elem = _recElems[elemIdx];
        plAssert(elem.nameIdx==evtx.nameIdx);

        // Update the elem
        if(evtx.flags&PL_FLAG_SCOPE_BEGIN) {
            // Update the path hash for sub-levels
            if(tc.curLevel<cmConst::MAX_LEVEL_QTY) {
                tc.levels[tc.curLevel].hashPath      = hashPath;
                tc.levels[tc.curLevel].parentNameIdx = evtx.nameIdx;
                tc.levels[tc.curLevel].prevElemIdx   = elemIdx;
            }
            // Save the elem point elem in this level, waiting for the "end" (to get the duration, which is the value we track, not the time)
            lc.elemTimeNs = evtx.vS64;
            lc.elemLIdx   = currentLIdx;
            if(_doForwardEvents) _itf->notifyFilteredEvent(elemIdx, evtx.flags, _recStrings[evtx.nameIdx].hash, evtx.vS64, 0);
        }
        else if(evtx.flags&PL_FLAG_SCOPE_END) {
            double value = (double)(evtx.vS64-lc.elemTimeNs); // value is the scope duration
            if(elem.absYMin>value) elem.absYMin = value;
            if(elem.absYMax<value) elem.absYMax = value;
            // "begin" lIdx and time
            INSERT_IN_ELEM(elem, elemIdx, lc.elemLIdx, lc.elemTimeNs, value, evtThreadBitmap);
            if(_doForwardEvents) _itf->notifyFilteredEvent(elemIdx, evtx.flags, _recStrings[evtx.nameIdx].hash, evtx.vS64, 0);
        }
        else if(eType>=PL_FLAG_TYPE_DATA_S32 && eType<=PL_FLAG_TYPE_DATA_STRING) {
            plAssert(level>0);
            double value = 0.;
            switch(eType) {
            case PL_FLAG_TYPE_DATA_S32:    value = (double)evtx.vInt; break;
            case PL_FLAG_TYPE_DATA_U32:    value = (double)evtx.vU32; break;
            case PL_FLAG_TYPE_DATA_S64:    value = (double)evtx.vS64; break;
            case PL_FLAG_TYPE_DATA_U64:    value = (double)evtx.vU64; break;
            case PL_FLAG_TYPE_DATA_FLOAT:  value = (double)evtx.vFloat; break;
            case PL_FLAG_TYPE_DATA_DOUBLE: value = (double)evtx.vDouble; break;
            case PL_FLAG_TYPE_DATA_STRING: value = (double)evtx.vStringIdx; break;
            default: plAssert(0, "Bug, an unknown event type is used");
            }
            if(elem.absYMin>value) elem.absYMin = value;
            if(elem.absYMax<value) elem.absYMax = value;
            INSERT_IN_ELEM(elem, elemIdx, currentLIdx, tc.levels[level-1].elemTimeNs, value, evtThreadBitmap);
            if(_doForwardEvents) _itf->notifyFilteredEvent(elemIdx, evtx.flags, _recStrings[evtx.nameIdx].hash, tc.levels[level-1].elemTimeNs, evtx.vU64);

            // If value is a string, also save an elem for it (used by search)
            if(eType==PL_FLAG_TYPE_DATA_STRING) {
                // Get the elem from the path hash
                u64 hashPath2     = bsHashStep(_recStrings[evtx.vStringIdx].hash, lc.hashPath);
                u64 partialItemHashPath2 = bsHashStep(evtx.flags, hashPath2);
                u64 itemHashPath2 = bsHashStep(tc.threadHash, partialItemHashPath2);
                int* elemIdxPtr2  = _recElemPathToId.find(itemHashPath2, evtx.vStringIdx);
                if(!elemIdxPtr2) { // If this Elem does not exist yet, let's create it
                    _recElems.push_back( { itemHashPath2, partialItemHashPath2, 0, evtx.vStringIdx, lc.prevElemIdx, evtx.threadId,
                            level, evtx.vStringIdx, tc.levels[level].parentNameIdx, evtx.flags, false, true, true } );
                    _recElemPathToId.insert(itemHashPath2, evtx.vStringIdx, _recElems.size()-1);
                }
                int elemIdx2 = elemIdxPtr2? *elemIdxPtr2:_recElems.size()-1;
                ElemBuild& elem2  = _recElems[elemIdx2];
                plAssert(elem2.nameIdx==evtx.vStringIdx);
                // Update the Elems
                value = evtx.vStringIdx;
                if(elem2.absYMin>value) elem2.absYMin = value;
                if(elem2.absYMax<value) elem2.absYMax = value;
                INSERT_IN_ELEM(elem2, elemIdx2, currentLIdx, tc.levels[level-1].elemTimeNs, value, evtThreadBitmap);
            }
        }
        else if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) {
            // Save standard Elem
            double value = 0.;
            if(elem.absYMin>value) elem.absYMin = value;
            if(elem.absYMax<value) elem.absYMax = value;
            INSERT_IN_ELEM(elem, elemIdx, currentLIdx, evtx.vS64, value, evtThreadBitmap);
            if(_doForwardEvents) _itf->notifyFilteredEvent(elemIdx, evtx.flags, _recStrings[evtx.nameIdx].hash, evtx.vS64, 0);
        }
        else if(eType==PL_FLAG_TYPE_MARKER) {
            // Save standard Elem (nameIdx=category) to be used by search on category
            double value = evtx.filenameIdx;
            if(elem.absYMin>value) elem.absYMin = value;
            if(elem.absYMax<value) elem.absYMax = value;
            INSERT_IN_ELEM(elem, elemIdx, currentLIdx, evtx.vS64, value, evtThreadBitmap);
            if(_doForwardEvents) _itf->notifyFilteredEvent(elemIdx, evtx.flags, _recStrings[evtx.nameIdx].hash, evtx.vS64, evtx.filenameIdx);

            // Save inversed Elem (filenameIdx=message) to be used by search on message content
            // Get the elem from the path hash
            u64 hashPath2     = bsHashStep(_recStrings[evtx.filenameIdx].hash, lc.hashPath);
            u64 partialItemHashPath2 = bsHashStep(evtx.flags, hashPath2);
            u64 itemHashPath2 = bsHashStep(tc.threadHash, partialItemHashPath2);
            int* elemIdxPtr2  = _recElemPathToId.find(itemHashPath2, evtx.filenameIdx);
            if(!elemIdxPtr2) { // If this Elem does not exist yet, let's create it
                _recElems.push_back( { itemHashPath2, partialItemHashPath2, 0, evtx.filenameIdx, lc.prevElemIdx, evtx.threadId,
                        level, evtx.filenameIdx, tc.levels[level].parentNameIdx, evtx.flags, false, true, true } );
                _recElemPathToId.insert(itemHashPath2, evtx.filenameIdx, _recElems.size()-1);
            }
            int elemIdx2 = elemIdxPtr2? *elemIdxPtr2:_recElems.size()-1;
            ElemBuild& elem2 = _recElems[elemIdx2];
            plAssert(elem2.nameIdx==evtx.filenameIdx);
            // Update the Elems
            value = evtx.filenameIdx;
            if(elem2.absYMin>value) elem2.absYMin = value;
            if(elem2.absYMax<value) elem2.absYMax = value;
            INSERT_IN_ELEM(elem2, elemIdx2, currentLIdx, evtx.vS64, value, evtThreadBitmap);
        }
    } // doStoreInHierarchy

    // Lock wait: additional processing required (MR display)
    if(eType==PL_FLAG_TYPE_LOCK_WAIT) {
        processLockWaitEvent(evtx, tc, level);
    }
    else if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) {
        // Forward lock notification events only if outside of the hierarchical tree
        // This compensate the fact that no non-scope element can be present at the root of the tree
        // So the scripting module will see lock notification from the tree and only the out-of-tree lock notifications if at the root.
        processLockNotifyEvent(evtx, tc, level, !doStoreInHierarchy);
    }
    else if(eType==PL_FLAG_TYPE_MARKER) {
        // Forward marker events only if outside of the hierarchical tree
        // This compensate the fact that no non-scope element can be present at the root of the tree
        // So the scripting library will see markers from the tree and only the out-of-tree marker if at the root.
        processMarkerEvent(evtx, tc, level, !doStoreInHierarchy);
    }

    // Mark the strings with the thread usage (used by search)
    if(!(evtx.flags&PL_FLAG_SCOPE_END) && !(_recStrings[evtx.nameIdx].threadBitmapAsName&evtThreadBitmap)) {
        cmRecord::String& s = _recStrings[evtx.nameIdx];
        s.threadBitmapAsName |= evtThreadBitmap;
        if(!s.isHexa) { // Used as a changed flag, only in this file
            s.isHexa = true;
            _recUpdatedStringIds.push_back(evtx.nameIdx);
        }
    }
    if(!(evtx.flags&PL_FLAG_SCOPE_END) && !(_recStrings[evtx.filenameIdx].threadBitmapAsName&evtThreadBitmap)) {
        cmRecord::String& s = _recStrings[evtx.filenameIdx];
        s.threadBitmapAsName |= evtThreadBitmap;
        if(!s.isHexa) { // Used as a changed flag, only in this file
            s.isHexa = true;
            _recUpdatedStringIds.push_back(evtx.filenameIdx);
        }
    }
    if(eType==PL_FLAG_TYPE_DATA_STRING && !(_recStrings[evtx.vStringIdx].threadBitmapAsName&evtThreadBitmap)) {
        cmRecord::String& s = _recStrings[evtx.vStringIdx];
        s.threadBitmapAsName |= evtThreadBitmap;
        if(!s.isHexa) { // Used as a changed flag, only in this file
            s.isHexa = true;
            _recUpdatedStringIds.push_back(evtx.vStringIdx);
        }
    }
}


void
cmRecording::updateDate(plPriv::EventExt& evtx, ShortDateState& sd)
{
    // Handle the short date and its potential wrap
    if(_isDateShort) {
        // Resynchronize the wrap quantity (once per buffer)
        if(sd.doResync && sd.lastEventBufferId!=_eventBufferId) {
            sd.lastEventBufferId = _eventBufferId; // Sync done
            if(sd.lastDateTick<_shortDateSyncTick) sd.lastDateTick = _shortDateSyncTick;
            sd.wrapPart = sd.lastDateTick&(~0xFFFFFFFFLL);
        }

        // Complete the short date with the wrap count, and update it if needed
        evtx.vS64 = sd.wrapPart | (evtx.vS64&0xFFFFFFFFLL);
        if(evtx.vS64<sd.lastDateTick-0x3FFFFFFFLL) {  // 1/4 wrap period in the past means a wrap
            evtx.vS64   += (1LL<<32);
            sd.wrapPart += (1LL<<32);
        }
        if(evtx.vS64<=sd.lastDateTick) evtx.vS64 = sd.lastDateTick+1; // Robustness against clock drift corrections on instrumentation side: ensure that dates are monotonous
        sd.lastDateTick = evtx.vS64;
    }

    // Unbias and stretch the time so that record start is 0 ns and is in nanosecond (i.e. conversion from tick to nanosecond)
    evtx.vS64 = (evtx.vS64>=_recTimeTickOrigin)? (s64)(_recTickToNs*(evtx.vS64-_recTimeTickOrigin)) : 0;
    // @#QUESTION Shall the monotonous characteristic of the date be enforced here too?

    // Keep track of the full record duration
    if(evtx.vS64>_recDurationNs) _recDurationNs = evtx.vS64;
}


bool
cmRecording::storeNewEvents(int streamId, plPriv::EventExt* events, int eventQty, s64 shortDateSyncTick)
{
    plScope("storeNewEvents");

    // Manage short date wrap
    if(_isDateShort) {
        _shortDateSyncTick = shortDateSyncTick; // Sync date is *before* any dates in this buffer. Null date will be ignored
        ++_eventBufferId;  // To track the synchronization done once per thread
    }

    // Loop on incoming events
    // =======================
    const bsVec<int>& streamStringLkup   = _recMStreamStringIdLkup[streamId];
    u8*               streamThreadIdLkup = _recMStreamThreadIdLkup[streamId];
    u8*               streamCoreIdLkup   = _recMStreamCoreIdLkup  [streamId];

    for(int i=0; i<eventQty; ++i) {

        plPriv::EventExt& evtx  = events[i];
        int               eType = evtx.flags&PL_FLAG_TYPE_MASK;

        if(_isMultiStream) {
            if(eType!=PL_FLAG_TYPE_ALLOC_PART && eType!=PL_FLAG_TYPE_DEALLOC_PART) {  // 1st part of memory events do not use strings

                if(eType!=PL_FLAG_TYPE_CSWITCH) {
                    // Strings
                    if(evtx.nameIdx>=(u32)streamStringLkup.size()) return false; // Means nameIdx is corrupted
                    evtx.nameIdx = streamStringLkup[evtx.nameIdx];
                    if(eType!=PL_FLAG_TYPE_SOFTIRQ) {
                        if(evtx.filenameIdx>=(u32)streamStringLkup.size()) return false; // Means filenameIdx is corrupted
                        evtx.filenameIdx = streamStringLkup[evtx.filenameIdx];
                    }
                    if(eType==PL_FLAG_TYPE_DATA_STRING) {
                        if(evtx.vStringIdx>=(u32)streamStringLkup.size()) return false; // Means the string data value is corrupted
                        evtx.vStringIdx = streamStringLkup[evtx.vStringIdx];
                    }
                }

                else {
                    // Context switch string
                    if(evtx.nameIdx!=0xFFFFFFFF && evtx.nameIdx!=0xFFFFFFFE) {
                        if(evtx.nameIdx>=(u32)streamStringLkup.size()) return false;  // Means that the context switch process name is corrupted
                        evtx.nameIdx = streamStringLkup[evtx.nameIdx];
                    }
                    // Context switch coreId
                    if(evtx.newCoreId!=0xFF) {
                        if(streamCoreIdLkup[evtx.newCoreId]==0xFF) {
                            int startCoreId = evtx.newCoreId;
                            while(startCoreId>0 && streamCoreIdLkup[startCoreId-1]==0xFF) --startCoreId;  // Assign all previous coreID also, to try to have them in order
                            while(startCoreId<=evtx.newCoreId) streamCoreIdLkup[startCoreId++] = (u8)(_recMStreamCoreQty++);
                        }
                        evtx.newCoreId = streamCoreIdLkup[evtx.newCoreId];
                    }
                    if(evtx.prevCoreId!=0xFF) {
                        if(streamCoreIdLkup[evtx.prevCoreId]==0xFF) {
                            int startCoreId = evtx.prevCoreId;
                            while(startCoreId>0 && streamCoreIdLkup[startCoreId-1]==0xFF) --startCoreId;  // Assign all previous coreID also, to try to have them in order
                            while(startCoreId<=evtx.prevCoreId) streamCoreIdLkup[startCoreId++] = (u8)(_recMStreamCoreQty++);
                        }
                        evtx.prevCoreId = streamCoreIdLkup[evtx.prevCoreId];
                    }
                }
            }  // End of memory event exclusion

            // Lock name: ensure that the lock name is not shared among streams. If it is the case, rename the subsequent ones
            if(eType>=PL_FLAG_TYPE_LOCK_FIRST && eType<=PL_FLAG_TYPE_LOCK_LAST && _recStrings[evtx.nameIdx].lockId>=0) {
                // A lock has already been created, so a name collision is possible and shall be checked
                LockBuild& lb = _recLocks[_recStrings[evtx.nameIdx].lockId];
                if(lb.mStreamNameLkup[streamId]<0) {
                    char newLockName[256];
                    snprintf(newLockName, sizeof(newLockName), "%s#%d", _recStrings[evtx.nameIdx].value.toChar(), streamId);  // Add "#<streamId>" to the lock name
                    _recStrings.push_back( { newLockName, "", bsHashString(newLockName), 0LL, 0, 1, -1, -1, false, false } );
                    lb.mStreamNameLkup[streamId] = _recStrings.size()-1;
                }
                evtx.nameIdx = lb.mStreamNameLkup[streamId];  // Conversion by the original lock lookup
            }
        } // End of multistream conversion & check

        // Case monostream: String integrity check (data corruption). Should never happen with "good" clients
        else if(eType!=PL_FLAG_TYPE_ALLOC_PART && eType!=PL_FLAG_TYPE_DEALLOC_PART) {  // 1st part of memory events do not use strings
            if(eType!=PL_FLAG_TYPE_CSWITCH) {
                if(evtx.nameIdx>=(u32)_recStrings.size()) return false; // Means nameIdx is corrupted
                if(eType!=PL_FLAG_TYPE_SOFTIRQ && evtx.filenameIdx>=(u32)_recStrings.size()) return false; // Means filenameIdx is corrupted
                if(eType==PL_FLAG_TYPE_DATA_STRING && evtx.vStringIdx>=(u32)_recStrings.size()) return false; // Means the string data value is corrupted
            }
            else if(evtx.nameIdx!=0xFFFFFFFF && evtx.nameIdx!=0xFFFFFFFE && evtx.nameIdx>=(u32)_recStrings.size()) {
                return false;  // Means that the context switch process name is corrupted
            }
        }

        // Core event case (stop processing if it concerns an external process, else continue for the ctx switch processing)
        if(eType==PL_FLAG_TYPE_CSWITCH) {
            if(!processCoreUsageEvent(streamId, evtx)) continue;
        }

        // Multistream conversion - part 2: convert threads after the core usage processing, to filter thread that do not belong to observed program
        if(_isMultiStream) {
            // Thread conversion
            if(streamThreadIdLkup[evtx.threadId]==0xFF) {
                streamThreadIdLkup[evtx.threadId] = (u8)(_recThreads.size());
            }
            evtx.threadId = streamThreadIdLkup[evtx.threadId];
        }

        // Get the associated thread context
        if(evtx.threadId>=_recThreads.size()) {

            if(evtx.threadId>=cmConst::MAX_THREAD_QTY) {
                LOG_ERROR(cmRecord::ERROR_MAX_THREAD_QTY_REACHED);
                continue; // Limitation due to optimized storage: 63 threads (other threads are ignored)
            }

            while(_recThreads.size()<=evtx.threadId) {
                plData("New thread ID", evtx.threadId);
                _recThreads.push_back(ThreadBuild());
                ThreadBuild& tc = _recThreads.back();
                tc.streamId = streamId;
                tc.shortDateStateCSwitch.doResync = false;  // Same as for CPU events

                // Reserve some space in the thread internal structures
                tc.memSSCurrentAlloc.reserve(256);
                tc.memSSEmptyIdx.reserve(256);
                tc.memDeallocMIdx.reserve(256);
                tc.memSnapshotIndexes.reserve(256);
                tc.memAllocChunkData.reserve(cmChunkSize);
                tc.memAllocChunkLocs.reserve(256);
                tc.memDeallocChunkData.reserve(cmChunkSize);
                tc.memDeallocChunkLocs.reserve(256);
                tc.memPlotChunkData.reserve(cmChunkSize);
                tc.memPlotChunkLocs.reserve(256);
                tc.ctxSwitchChunkData.reserve(cmChunkSize);
                tc.ctxSwitchChunkLocs.reserve(256);
                tc.softIrqChunkData.reserve(cmChunkSize);
                tc.softIrqChunkLocs.reserve(256);
                tc.lockWaitChunkData.reserve(cmChunkSize);
                tc.lockWaitChunkLocs.reserve(256);
                tc.lockWaitNameIdxs.reserve(256);
                tc.levels.reserve(8);
            }
        }
        ThreadBuild& tc = _recThreads[evtx.threadId];

        // Thread updates
        if(tc.threadHash==0) {
            tc.threadHash       = 0x10000+evtx.threadId; // Arbitrary but unique thread hash
            tc.threadUniqueHash = tc.threadHash;         // Equal to threadHash, unless a name is given later to the thread
        }
        if(eType==PL_FLAG_TYPE_THREADNAME) {
            plAssert(evtx.nameIdx<(u32)_recStrings.size(), "Weird, Palanteer client references a string without sending it first", evtx.nameIdx, _recStrings.size());
            if(tc.nameIdx<0) { // Only first call matters
                tc.nameIdx          = evtx.nameIdx;
                tc.threadUniqueHash = _recStrings[evtx.nameIdx].hash;
                _recNameUpdatedThreadIds.push_back(evtx.threadId);
                _itf->notifyNewThread(evtx.threadId, tc.threadUniqueHash); // Notify the interface only for named threads
            }
            continue;
        }

        // Convert dates from tick to nanoseconds
        if(eType!=PL_FLAG_TYPE_CSWITCH &&  // Ctx switch dates have already been processed
           (eType==PL_FLAG_TYPE_DATA_TIMESTAMP || (eType>=PL_FLAG_TYPE_WITH_TIMESTAMP_FIRST && eType<=PL_FLAG_TYPE_WITH_TIMESTAMP_LAST))) {
            updateDate(evtx, (eType==PL_FLAG_TYPE_SOFTIRQ)? tc.shortDateStateCSwitch : tc.shortDateState);
            if(evtx.vS64>tc.durationNs) tc.durationNs = evtx.vS64;
        }

        // Lock usage case (cannot be handled in a hierarchical manner)
        int secondEventFlags = PL_FLAG_TYPE_DATA_NONE; // No second event
        if(eType==PL_FLAG_TYPE_LOCK_ACQUIRED || eType==PL_FLAG_TYPE_LOCK_RELEASED) {
            // This call returns false if the lock event is a duplicated one.
            // If a lock wait end shall be inserted before this one, the boolean doInsertLockWaitEnd is set
            bool doInsertLockWaitEnd = false;
            bool doProcessLockUse    = processLockUseEvent(streamId, evtx, doInsertLockWaitEnd);
            if(!doProcessLockUse && !doInsertLockWaitEnd) continue;

            // In case of lock wait insertion, we turn the lock into a lock wait end now (change in level)
            //  and indicate that the original bugz shall be logged afterward
            if(doInsertLockWaitEnd) {
                // Indicate the flags of the second event if the lock use shall be processed
                if(doProcessLockUse) secondEventFlags = evtx.flags;
                // Mutate the current event into a wait end (which modifies the level)
                evtx.flags = PL_FLAG_TYPE_LOCK_WAIT | PL_FLAG_SCOPE_END;
            }
        }

        // Context switch and SOFTIRQ cases
        if(eType==PL_FLAG_TYPE_CSWITCH) {
            processCtxSwitchEvent(evtx, tc);
            continue;
        }
        if(eType==PL_FLAG_TYPE_SOFTIRQ) {
            processSoftIrqEvent(evtx, tc);
            continue;
        }

        // Manage the level change
        if(evtx.flags&PL_FLAG_SCOPE_END) {
            if(tc.curLevel>0) --tc.curLevel;
            else {
                LOG_ERROR(cmRecord::ERROR_TOP_LEVEL_REACHED);
                continue;
            }
            if(tc.curLevel==cmConst::MAX_LEVEL_QTY-1) continue; // Asymmetry between begin&close for a given level number
        }

        int level = tc.curLevel;
        if(evtx.flags&PL_FLAG_SCOPE_BEGIN) ++tc.curLevel;
        if(level>=cmConst::MAX_LEVEL_QTY) {
            LOG_ERROR(cmRecord::ERROR_MAX_LEVEL_QTY_REACHED);
            continue;
        }
        while(tc.curLevel>=tc.levels.size() && tc.curLevel<cmConst::MAX_LEVEL_QTY) {
            // Add a nesting level
            tc.levels.push_back(NestingLevelBuild());
            NestingLevelBuild& lc = tc.levels.back();
            if(tc.levels.size()==1) {
                lc.hashPath      = bsHashStepChain(cmConst::SCOPE_NAMEIDX);  // First level: scope ID constant
                lc.parentNameIdx = PL_INVALID;
                lc.parentFlags   = 0;
            } else {
                lc.hashPath      = bsHashStep(_recStrings[evtx.nameIdx].hash, tc.levels[tc.levels.size()-2].hashPath); // Next levels: nameId hashed with previous path
                lc.parentNameIdx = evtx.nameIdx;
                lc.parentFlags   = evtx.flags;
            }
            lc.scopeChunkData.reserve(cmChunkSize);
            lc.nonScopeChunkData.reserve(cmChunkSize);
            lc.lastMrScopeSpeckChunksIndexes.reserve(8);
            lc.mrScopeSpeckChunks.reserve(8);
        }

        // Memory case
        if((eType>=PL_FLAG_TYPE_MEMORY_FIRST && eType<=PL_FLAG_TYPE_MEMORY_LAST)) {
            processMemoryEvent(evtx, tc, level);
            continue;
        }

        // Generic event case
        // ==================

        // Log the event in the hierarchical tree
        processScopeEvent(evtx, tc, level);

        // Optional insertion of a second event, copy of the first one but with flag changed
        // Typically for the lock use case which requires closing the lock wait scope
        if(secondEventFlags!=PL_FLAG_TYPE_DATA_NONE) {
            evtx.flags = (u8)secondEventFlags;
            processScopeEvent(evtx, tc, level);
        }

    } // End of loop on events

    return true;
}


void
cmRecording::writeGenericChunk(bsVec<cmRecord::Evt>& chunkData, bsVec<u64>& chunkLocs)
{
    if(chunkData.empty()) return;
    if(!_recFd) { // No recording case
        chunkData.clear();
        return;
    }
    plgScope(REC, "writeGenericChunk");

    // Store the compressed raw chunk in the big event file and register it for this nesting level
    plgBegin(REC, "Disk write");
    int writtenBufferSize = sizeof(cmRecord::Evt)*chunkData.size();
    if(_isCompressionEnabled) {
        plgBegin(REC, "Compression");
        writtenBufferSize = _workingCompressionBuffer.size(); // Big enough for output, adjusted by the compression function to match the output
        cmCompressChunk((u8*)&chunkData[0], sizeof(cmRecord::Evt)*chunkData.size(), &_workingCompressionBuffer[0], &writtenBufferSize);
        plgEnd(REC, "Compression");
        fwrite(&_workingCompressionBuffer[0], 1, writtenBufferSize, _recFd);
    } else {
        fwrite(&chunkData[0], 1, writtenBufferSize, _recFd);
    }
    chunkLocs.push_back(cmRecord::makeChunkLoc(_recLastEventFileOffset, writtenBufferSize));
    _recLastEventFileOffset += writtenBufferSize;
    chunkData.clear();
    plgEnd(REC, "Disk write");
}


void
cmRecording::writeElemChunk(ElemBuild& elem, bool isLast)
{
    if(!_recFd) { // No recording case
        elem.chunkLIdx.clear();
        elem.chunkTimes.clear();
        elem.chunkValues.clear();
        return;
    }
    plgScope(REC, "writeElemChunk");

    _workingNewMRElems.clear();
    _workingNewMRElemValues.clear();
    u32 realSize = elem.chunkLIdx.size();

    // Store the raw chunk in the big event file
    if(realSize) {
        // Store the raw chunk in the big elem file and register it for this elem
        plgBegin(REC, "Disk write");
        int writtenBufferSize = sizeof(u32)*realSize;
        if(_isCompressionEnabled) {
            plgBegin(REC, "Compression");
            writtenBufferSize = _workingCompressionBuffer.size(); // Big enough for output, adjusted by the compression function to match the output
            cmCompressChunk((u8*)&elem.chunkLIdx[0], sizeof(u32)*realSize, &_workingCompressionBuffer[0], &writtenBufferSize);
            plgEnd(REC, "Compression");
            fwrite(&_workingCompressionBuffer[0], 1, writtenBufferSize, _recFd);
        } else {
            plgBegin(REC, "Disk write");
            fwrite(&elem.chunkLIdx[0], 1, writtenBufferSize, _recFd);
        }
        elem.chunkLocs.push_back(cmRecord::makeChunkLoc(_recLastEventFileOffset, writtenBufferSize));
        _recLastEventFileOffset += writtenBufferSize;
        plgEnd(REC, "Disk write");

        // Compute the first MR speck size, lIdx and value
        plgBegin(REC, "Compute MR level 0");
        if(elem.doRepresentScope) {
            // The speck is the biggest gap between two points. Suitable for agglomerated scopes representation
            for(u32 j=0; j<realSize; j+=cmMRElemSize) {
                s64 speckNs     = elem.chunkTimes[j]-elem.lastTimeNs;
                u32 selectedIdx = bsMin(j+cmMRElemSize-1, realSize-1);  // Take the last one in case of equal values
                for(u32 i=j; i<j+cmMRElemSize-1 && i<realSize-1; ++i) {
                    speckNs = bsMax(speckNs, elem.chunkTimes[i+1]-elem.chunkTimes[i]);
                    if(elem.chunkValues[selectedIdx]<elem.chunkValues[i]) selectedIdx = i; // Hardcoded choice: highest value
                    elem.lastTimeNs = elem.chunkTimes[i+1];
                }
                _workingNewMRElems     .push_back( { (u32)(speckNs>>10), elem.chunkLIdx[selectedIdx] } ); // Speck Size unit is Ns/1024
                _workingNewMRElemValues.push_back(elem.chunkValues[selectedIdx]);
            }
        }
        else {
            // The speck is the time delta with the full resolution start point. Suitable for plot representation (i.e. subsampling)
            bool takeMax = true;
            for(u32 j=0; j<realSize; j+=cmMRElemSize) {
                u32 selectedIdx = bsMin(j+cmMRElemSize-1, realSize-1);
                if(takeMax) {  // Hardcoded choice: alternate highest and lowest values
                    for(u32 i=j; i<j+cmMRElemSize-1 && i<realSize-1; ++i) {
                        if(elem.chunkValues[selectedIdx]<elem.chunkValues[i]) selectedIdx = i;
                    }
                } else {
                    for(u32 i=j; i<j+cmMRElemSize-1 && i<realSize-1; ++i) {
                        if(elem.chunkValues[selectedIdx]>elem.chunkValues[i]) selectedIdx = i;
                    }
                }
                s64 speckNs     = elem.chunkTimes[bsMin(j+cmMRElemSize, realSize)-1]-elem.lastTimeNs;
                elem.lastTimeNs = elem.chunkTimes[bsMin(j+cmMRElemSize, realSize)-1];
                _workingNewMRElems     .push_back( { (u32)(speckNs>>10), elem.chunkLIdx[selectedIdx] } ); // Speck Size unit is Ns/1024
                _workingNewMRElemValues.push_back(elem.chunkValues[selectedIdx]);
                takeMax = !takeMax;
            }
        }
        plgEnd(REC, "Compute MR level 0");

        // Prepare next chunk
        elem.chunkLIdx.clear();
        elem.chunkTimes.clear();
        elem.chunkValues.clear();
    }

    // Update the multi-resolution pyramid for scope data
    plgBegin(REC, "Update MR pyramid");
    bsVec<bsVec<cmRecord::ElemMR> >& h  = elem.mrSpeckChunks;
    bsVec<bsVec<double> >&           hv = elem.workMrValues;
    if(!_workingNewMRElems.empty() && h.empty()) {
        h .push_back( { } );
        hv.push_back( { } );
    }

    // Update the pyramid
    for(int bidx=0; bidx<_workingNewMRElems.size(); ++bidx) {

        // Add the new data in the MR level 0
        h [0].push_back(_workingNewMRElems[bidx]);
        hv[0].push_back(_workingNewMRElemValues[bidx]);

        // Update the pyramid
        for(int hLvl=0; hLvl<h.size(); ++hLvl) {
            bsVec<cmRecord::ElemMR>& hl  = h[hLvl];
            bsVec<double>&           hlv = hv[hLvl];
            int hlSize = hl.size();
            if(hlSize==1) break;                // Last level has size one and means that we stop here
            if((hlSize%cmMRElemSize)!=0) break; // This level is not complete, nothing more to build at the moment

            // Aggregate the speck size on the last batch of data and add it in upper hierarchical level
            u32 speckUs = 0;
            u32 selectedIdx = hlSize-cmMRElemSize;  // Favorise the latest point in case of equal values
            bool isMax = elem.doRepresentScope || (h.size()==hLvl+1) || (h[hLvl+1].size()&1)==0;
            for(int i=hlSize-cmMRElemSize; i<hlSize; ++i) {
                if(elem.doRepresentScope) speckUs  = bsMax(speckUs, hl[i].speckUs); // Density     rule
                else                      speckUs += hl[i].speckUs;                 // Subsampling rule
                if(isMax) {
                    if(hlv[selectedIdx]<hlv[i]) selectedIdx = i;
                } else {
                    if(hlv[selectedIdx]>hlv[i]) selectedIdx = i;
                }
            }
            // Add an entry in upper hierarchical level
            if(h.size()==hLvl+1) {
                h .push_back(bsVec<cmRecord::ElemMR>()); h .back().reserve(cmMRElemSize);
                hv.push_back(bsVec<double>());           hv.back().reserve(cmMRElemSize);
            }
            h [hLvl+1].push_back( { speckUs, h[hLvl][selectedIdx].lIdx } );
            hv[hLvl+1].push_back(hv[hLvl][selectedIdx]);
        }
    }
    plgEnd(REC, "Update MR pyramid");

    // Last call case: finalize the pyramid
    if(!isLast) return;
    bool lastLevelModified = false;
    for(int hLvl=0; hLvl<h.size(); ++hLvl) {
        bsVec<cmRecord::ElemMR>& hl  = h[hLvl];
        bsVec<double>&           hlv = hv[hLvl];
        int hlSize = hl.size();
        if(hlSize==1) break;
        int remainingChunkSize = hlSize%cmMRElemSize;
        if(!lastLevelModified && remainingChunkSize==0) continue;
        if(remainingChunkSize==0) remainingChunkSize += cmMRElemSize;

        // Aggregate the speck size on the last batch of data and add it in upper hierarchical level
        u32 speckUs = 0;
        u32 selectedIdx   = hlSize-remainingChunkSize;
        for(int i=hlSize-remainingChunkSize; i<hlSize; ++i) {
            if(elem.doRepresentScope) speckUs  = bsMax(speckUs, hl[i].speckUs); // Density     rule
            else                      speckUs += hl[i].speckUs;                 // Subsampling rule
            if(hlv[selectedIdx]<hlv[i]) selectedIdx = i;
        }
        // Add an entry in upper hierarchical level
        if(h.size()==hLvl+1) {
            h .push_back(bsVec<cmRecord::ElemMR>()); h .back().reserve(cmMRElemSize);
            hv.push_back(bsVec<double>());    hv.back().reserve(cmMRElemSize);
        }
        h [hLvl+1].push_back( { speckUs, h[hLvl][selectedIdx].lIdx } );
        hv[hLvl+1].push_back(hv[hLvl][selectedIdx]);
        lastLevelModified = true;
    }

    // Check pyramidal structure (done once per level at the end of the record)
    for(int hLvl=0; hLvl<h.size()-1; ++hLvl) {
        plAssert((h[hLvl].size()+cmMRElemSize-1)/cmMRElemSize==h[hLvl+1].size(), "Internal bug");
    }
}


void
cmRecording::writeScopeChunk(NestingLevelBuild& lc, bool isLast)
{
    if(!_recFd) { // No recording case
        lc.scopeChunkData.clear();
        return;
    }
    plgScope(REC, "writeScopeChunk");

    // Store the raw chunk in the big event file
    u32 realSize = lc.scopeChunkData.size()&0xFFFFFFFE; // Always even size
    _workingNewMRScopes.clear();
    if(realSize) {
        // Store the raw chunk in the big event file and register it for this nesting level
        plgBegin(REC, "Disk write");
        int writtenBufferSize = sizeof(cmRecord::Evt)*realSize;
        if(_isCompressionEnabled) {
            plgBegin(REC, "Compression");
            writtenBufferSize = _workingCompressionBuffer.size(); // Big enough for output, adjusted by the compression function to match the output
            cmCompressChunk((u8*)&lc.scopeChunkData[0], sizeof(cmRecord::Evt)*realSize, &_workingCompressionBuffer[0], &writtenBufferSize);
            plgEnd(REC, "Compression");
            fwrite(&_workingCompressionBuffer[0], 1, writtenBufferSize, _recFd);
       } else {
            fwrite(&lc.scopeChunkData[0], 1, writtenBufferSize, _recFd);
        }
        lc.scopeChunkLocs.push_back(cmRecord::makeChunkLoc(_recLastEventFileOffset, writtenBufferSize));
        _recLastEventFileOffset += writtenBufferSize;
        plgEnd(REC, "Disk write");

        // Compute the first MR speck size scopes
        plgBegin(REC, "Compute MR level 0");
        for(u32 j=0; j<realSize; j+=cmMRScopeSize) {
            s64 speckNs = lc.scopeChunkData[j].vS64-lc.writeScopeLastTimeNs;
            for(u32 i=j; i<j+cmMRScopeSize-1 && i<realSize-1; ++i) speckNs = bsMax(speckNs, lc.scopeChunkData[i+1].vS64-lc.scopeChunkData[i].vS64);
            _workingNewMRScopes.push_back((u32)(speckNs>>10)); // Unit is Ns/1024 (roughly millisecond...)
            lc.writeScopeLastTimeNs = lc.scopeChunkData[bsMin(j+cmMRScopeSize-2, realSize-2)].vS64;
        }
        plgEnd(REC, "Compute MR level 0");

        // Prepare next chunk
        lc.scopeChunkData.clear();
    }

    // Update the multi-resolution pyramid for scope data
    bsVec<bsVec<u32>>& h = lc.mrScopeSpeckChunks;
    if(!_workingNewMRScopes.empty() && h.empty()) {
        h.push_back( { } ); h.back().reserve(cmChunkSize);
    }

    // Update the pyramid
    plgBegin(REC, "Update MR pyramid");
    for(int bidx=0; bidx<_workingNewMRScopes.size(); ++bidx) {
        // Add the new data in the MR level 0
        h[0].push_back(_workingNewMRScopes[bidx]);

        // Update the pyramid
        for(int hLvl=0; hLvl<h.size(); ++hLvl) {
            bsVec<u32>& hl = h[hLvl];
            int hlSize = hl.size();
            if(hlSize==1) break;                 // Last level has size one and means that we stop here
            if((hlSize%cmMRScopeSize)!=0) break; // This level is not complete, nothing more to build at the moment

            // Aggregate the speck size on the last batch of data and add it in upper hierarchical level
            u32 speckUs = 0; for(int i=hlSize-cmMRScopeSize; i<hlSize; ++i) speckUs = bsMax(speckUs, hl[i]);
            if(h.size()==hLvl+1) { h.push_back(bsVec<u32>()); h.back().reserve(cmChunkSize); }
            h[hLvl+1].push_back(speckUs);
        }
    }
    plgEnd(REC, "Update MR pyramid");

    // Last call case: finalize the pyramid
    if(!isLast) return;
    bool lastLevelModified = false;
    for(int hLvl=0; hLvl<h.size(); ++hLvl) {
        bsVec<u32>& hl = h[hLvl];
        int hlSize = hl.size();
        if(hlSize==1) break;
        int remainingChunkSize = hlSize%cmMRScopeSize;
        if(!lastLevelModified && remainingChunkSize==0) continue;
        if(remainingChunkSize==0) remainingChunkSize += cmMRScopeSize;

        // Aggregate the speck size on the last batch of data and add it in upper hierarchical level
        u32 speckUs = 0; for(int i=hlSize-remainingChunkSize; i<hlSize; ++i) speckUs = bsMax(speckUs, hl[i]);
        if(h.size()==hLvl+1) { h.push_back(bsVec<u32>()); h.back().reserve(cmMRScopeSize); }
        h[hLvl+1].push_back(speckUs);
        lastLevelModified = true;
    }

    // Check pyramidal structure (done once per level at the end of the record)
    for(int hLvl=0; hLvl<h.size()-1; ++hLvl) {
        plAssert((h[hLvl].size()+cmMRScopeSize-1)/cmMRScopeSize==h[hLvl+1].size(), "Internal bug");
    }
}


// ==============================================================================================
// Record structure layer
// ==============================================================================================

void
cmRecording::endRecord(void)
{
    plScope("endRecord");

    if(!_recFd) { // No recording case
        _recordAppName.clear();
        _recThreads.clear();
        _recStrings.clear();
        return;
    }

    // Record "polishing"
    // ==================

    // Search for the empty string
    u32 emptyIdx = 0;
    while(emptyIdx<(u32)_recStrings.size() && _recStrings[emptyIdx].hash!=_hashEmptyString) ++emptyIdx;
    if(emptyIdx==(u32)_recStrings.size()) storeNewString(0, "", _hashEmptyString);

    // Force the closing of all open blocks
    plPriv::EventExt endEvtx = { 0, PL_FLAG_TYPE_DATA_TIMESTAMP | PL_FLAG_SCOPE_END, 0, { emptyIdx } , { emptyIdx }, 0, {0} };
    endEvtx.vS64 = _recDurationNs;
    for(int threadId=0; threadId<_recThreads.size(); ++threadId) {
        ThreadBuild& tc  = _recThreads[threadId];
        endEvtx.threadId = (u8)threadId;
        // Scopes
        for(int level=tc.levels.size()-1; level>=0; --level) {
            NestingLevelBuild& lc = tc.levels[level];
            if(!lc.isScopeOpen) continue;
            if(level==cmConst::MAX_LEVEL_QTY-1) continue; // End event is offset by 1
            endEvtx.threadId = (u8)threadId;
            processScopeEvent(endEvtx, tc, level);
            endEvtx.nameIdx = emptyIdx; // Re-set it as it was "corrected" during the processing
        }
        // Soft IRQs
        if(tc.isSoftIrqScopeOpen) {
            processSoftIrqEvent(endEvtx, tc);
        }
    }

    // Flush global elems
    plgData(REC, "Flush lock notif events", _recGlobal.lockNtfChunkData.size());
    writeGenericChunk(_recGlobal.lockNtfChunkData,  _recGlobal.lockNtfChunkLocs);
    plgData(REC, "Flush lock use events", _recGlobal.lockUseChunkData.size());
    writeGenericChunk(_recGlobal.lockUseChunkData,  _recGlobal.lockUseChunkLocs);
    plgData(REC, "Flush core use events", _recGlobal.coreUsageChunkData.size());
    writeGenericChunk(_recGlobal.coreUsageChunkData,  _recGlobal.coreUsageChunkLocs);
    plgData(REC, "Flush marker events", _recGlobal.markerChunkData.size());
    writeGenericChunk(_recGlobal.markerChunkData,  _recGlobal.markerChunkLocs);
    plgText(REC, "Stage", "Flush elems");
    for(auto& elem : _recElems) {
        writeElemChunk(elem, true);
    }

    // Flush thread incomplete chunks (memory & switches & generic events)
    for(int tId=0; tId<_recThreads.size(); ++tId) {
        auto& tc = _recThreads[tId];
        plgScope (REC, "Flush thread");
        plgData(REC, "Thread index", tId);
        plgData(REC, "Flush alloc events",     tc.memAllocChunkData.size());
        writeGenericChunk(tc.memAllocChunkData,  tc.memAllocChunkLocs);
        plgData(REC, "Flush dealloc events",   tc.memDeallocChunkData.size());
        writeGenericChunk(tc.memDeallocChunkData, tc.memDeallocChunkLocs);
        plgData(REC, "Flush mem plot events",  tc.memPlotChunkData.size());
        writeGenericChunk(tc.memPlotChunkData,   tc.memPlotChunkLocs);
        plgData(REC, "Flush ctx switch plot events", tc.ctxSwitchChunkData.size());
        writeGenericChunk(tc.ctxSwitchChunkData, tc.ctxSwitchChunkLocs);
        plgData(REC, "Flush softIrq events",   tc.softIrqChunkData.size());
        writeGenericChunk(tc.softIrqChunkData,   tc.softIrqChunkLocs);
        plgData(REC, "Flush lock wait events", tc.lockWaitChunkData.size());
        writeGenericChunk(tc.lockWaitChunkData,  tc.lockWaitChunkLocs);
        for(auto& lc : tc.levels) {
            plgScope(REC, "Completion");
            plgData(REC, "Flush non-scope events", lc.nonScopeChunkData.size());
            writeGenericChunk(lc.nonScopeChunkData,   lc.nonScopeChunkLocs);
            plgData(REC, "Flush scope events", lc.scopeChunkData.size());
            writeScopeChunk(lc, true);
        }
        while(!tc.levels.empty() && tc.levels.back().scopeChunkLocs.empty() && tc.levels.back().nonScopeChunkLocs.empty()) {
            tc.levels.pop_back();
            plgText(REC, "Stage", "***One nesting level removed");
        }
    }

    // Write of the meta informations at the end of the record file
    // =============================================================
    // Get the meta information header position
    s64 headerStartOffset = bsOsFtell(_recFd);

    // Write generic data
    // ==================

    plgText(REC, "Stage", "Write the record format version");
    u32 tmp = PL_RECORD_FORMAT_VERSION;
    fwrite(&tmp, 4, 1, _recFd);

    plgText(REC, "Stage", "Write the application name");
    tmp = _recordAppName.size();
    fwrite(&tmp,               4,   1, _recFd);
    fwrite(&_recordAppName[0], 1, tmp, _recFd);

    plgData(REC, "Write the thread quantity", _recThreads.size());
    tmp = _recThreads.size();
    fwrite(&tmp, 4,   1, _recFd);

    plgData(REC, "Write the core quantity", _recCoreQty); // 0 if no context switch
    fwrite(&_recCoreQty, 4, 1, _recFd);

    plgData(REC, "Write the string quantity", _recStrings.size());
    tmp = _recStrings.size();
    fwrite(&tmp, 4, 1, _recFd);

    plgData(REC, "Write the compression mode", _isCompressionEnabled);
    tmp = _isCompressionEnabled? 1 : 0;
    fwrite(&tmp, 4, 1, _recFd);

    plgData(REC, "Write the multistream mode", _isMultiStream);
    tmp = _isMultiStream? 1 : 0;
    fwrite(&tmp, 4, 1, _recFd);

    // Write the global event qty
    // We cannot recompute it fully from thread as some are thread-less (lock use, ctx switch...)
    fwrite(&_recElemEventQty,      4, 1, _recFd);
    fwrite(&_recMemEventQty,       4, 1, _recFd);
    fwrite(&_recCtxSwitchEventQty, 4, 1, _recFd);
    fwrite(&_recLockEventQty,      4, 1, _recFd);
    fwrite(&_recMarkerEventQty,    4, 1, _recFd);

    // Write the streams
    // =================
    tmp = _recStreams.size();
    plgData(REC, "Write the stream quantity", tmp);
    fwrite(&tmp, 4, 1, _recFd);
    for(const cmStreamInfo& si : _recStreams) {
        // App name (for this stream. Same as global app name in case of monostream)
        tmp = si.appName.size();
        fwrite(&tmp, 4, 1, _recFd);
        if(tmp) fwrite(&si.appName[0], 1, tmp, _recFd);
        // Build name
        tmp = si.buildName.size();
        fwrite(&tmp, 4, 1, _recFd);
        if(tmp) fwrite(&si.buildName[0], 1, tmp, _recFd);
        // Lang name
        tmp = si.langName.size();
        fwrite(&tmp, 4, 1, _recFd);
        if(tmp) fwrite(&si.langName[0], 1, tmp, _recFd);
        tmp = PL_TLV_QTY;
        fwrite(&tmp, 4, 1, _recFd);
        fwrite(&si.tlvs[0], 8, PL_TLV_QTY, _recFd);
    }

    // Write the strings
    // =================
    for(const cmRecord::String& s : _recStrings) {
        tmp = s.value.size();
        fwrite(&tmp,          4,   1, _recFd);
        if(tmp) fwrite(&s.value[0],   1, tmp, _recFd);
        fwrite(&s.hash,       8,   1, _recFd);
        fwrite(&s.threadBitmapAsName, 8, 1, _recFd);
        fwrite(&s.lockId,     4,   1, _recFd);
        fwrite(&s.categoryId, 4,   1, _recFd);
    }

    // Write threads
    // =============

    for(int tId=0; tId<_recThreads.size(); ++tId) {
        auto& tc = _recThreads[tId];
        plgScope(REC, "Thread");
        plgData(REC, "NameIdx", tc.nameIdx);
        plgData(REC, "Nesting levels", tc.levels.size());

        // Write the thread context
        // ========================

        // Write the thread streamId
        fwrite(&tc.streamId, 4, 1, _recFd);

        // Write the thread nameIdx
        fwrite(&tc.nameIdx, 4, 1, _recFd);

        // Write the thread hash (not the unique one, but the one used in the element hashing)
        fwrite(&tc.threadHash, 8, 1, _recFd);

        // Write the thread end time
        fwrite(&tc.durationNs, 8, 1, _recFd);

        // Write the thread event qty
        fwrite(&tc.elemEventQty,      4, 1, _recFd);
        fwrite(&tc.memEventQty,       4, 1, _recFd);
        fwrite(&tc.ctxSwitchEventQty, 4, 1, _recFd);
        fwrite(&tc.lockEventQty,      4, 1, _recFd);
        fwrite(&tc.markerEventQty,    4, 1, _recFd);

        // Write the quantity of nesting levels
        tmp = tc.levels.size();
        fwrite(&tmp, 4, 1, _recFd);

        // Loop on nesting levels
        for(auto& lc : tc.levels) {
            plgScope(REC, "Nesting level");

            // Write the chunk indexes for this nesting level
            tmp = lc.nonScopeChunkLocs.size();
            fwrite(&tmp, 4, 1, _recFd);
            plgData(REC, "Non scope chunks", tmp);
            if(tmp) fwrite(&lc.nonScopeChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);
            tmp = lc.scopeChunkLocs.size();
            fwrite(&tmp, 4, 1, _recFd);
            plgData(REC, "Scope chunks", tmp);
            if(tmp) fwrite(&lc.scopeChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

            // Write the MR scope levels
            tmp = lc.mrScopeSpeckChunks.size();
            fwrite(&tmp, 4, 1, _recFd);
            plgData(REC, "MR levels", tmp);

            // Loop on resolution levels
            for(const bsVec<u32>& entries : lc.mrScopeSpeckChunks) {
                plgScope(REC, "MR level");
                tmp = entries.size();
                fwrite(&tmp, 4, 1, _recFd);
                plgData(REC, "size", tmp);
                fwrite(&entries[0], sizeof(u32), tmp, _recFd);
            } // End of loop on multi-resolution levels
        } // End of loop on nested levels

        // Write the memory indexes
        tmp = tc.memAllocChunkLocs.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "Memory alloc chunks", tmp);
        if(tmp) fwrite(&tc.memAllocChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);
        tmp = tc.memDeallocChunkLocs.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "Memory dealloc chunks", tmp);
        if(tmp) fwrite(&tc.memDeallocChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);
        tmp = tc.memPlotChunkLocs.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "Memory plot chunks", tmp);
        if(tmp) fwrite(&tc.memPlotChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

        // Write the memory deallocation index lookup (so that each alloc knows its dealloc directly)
        tmp = tc.memDeallocMIdx.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "Memory dealloc lookup size", tmp);
        if(tmp) fwrite(&tc.memDeallocMIdx[0], sizeof(u32), tmp, _recFd);

        // Write the memory snapshot indexes
        tmp = tc.memSnapshotIndexes.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "Memory snapshot indexes size", tmp);
        if(tmp) fwrite(&tc.memSnapshotIndexes[0], sizeof(cmRecord::MemSnapshot), tmp, _recFd);

        // Write the context switches indexes
        tmp = tc.ctxSwitchChunkLocs.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "Context switched indexes size", tmp);
        if(tmp) fwrite(&tc.ctxSwitchChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

        // Write the softIrq indexes
        tmp = tc.softIrqChunkLocs.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "SOFTIRQ indexes size", tmp);
        if(tmp) fwrite(&tc.softIrqChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

        // Write the lock indexes
        tmp = tc.lockWaitChunkLocs.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "Lock waits indexes size", tmp);
        if(tmp) fwrite(&tc.lockWaitChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);
    } // End of loop on threads

    // Write the core usage indexes
    tmp = _recGlobal.coreUsageChunkLocs.size();
    fwrite(&tmp, 4, 1, _recFd);
    plgData(REC, "Core usage indexes size", tmp);
    if(tmp) fwrite(&_recGlobal.coreUsageChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

    // Write the marker indexes
    tmp = _recGlobal.markerChunkLocs.size();
    fwrite(&tmp, 4, 1, _recFd);
    plgData(REC, "Marker indexes size", tmp);
    if(tmp) fwrite(&_recGlobal.markerChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

    // Write the marker categories
    tmp = _recMarkerCategoryNameIdxs.size();
    fwrite(&tmp, 4, 1, _recFd);
    plgData(REC, "Category list size", tmp);
    if(tmp) fwrite(&_recMarkerCategoryNameIdxs[0], sizeof(int), tmp, _recFd);

    // Write the locks
    // ===============
    // Write the lock notification indexes
    tmp = _recGlobal.lockNtfChunkLocs.size();
    fwrite(&tmp, 4, 1, _recFd);
    plgData(REC, "Lock notification indexes size", tmp);
    if(tmp) fwrite(&_recGlobal.lockNtfChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

    // Write the lock use indexes
    tmp = _recGlobal.lockUseChunkLocs.size();
    fwrite(&tmp, 4, 1, _recFd);
    plgData(REC, "Lock use indexes size", tmp);
    if(tmp) fwrite(&_recGlobal.lockUseChunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

    // Write the lock array
    tmp = _recLocks.size();
    fwrite(&tmp, 4, 1, _recFd);
    for(int i=0; i<_recLocks.size(); ++i) {
        LockBuild& lock = _recLocks[i];
        // NameIdx of the lock
        fwrite(&lock.nameIdx, sizeof(int), 1, _recFd);
        // List of threadId using this lock
        tmp = lock.waitingThreadIds.size();
        fwrite(&tmp, 4, 1, _recFd);
        if(tmp) {
            std::sort(lock.waitingThreadIds.begin(), lock.waitingThreadIds.end()); // Have fixed canonical order
            fwrite(&lock.waitingThreadIds[0], sizeof(int), tmp, _recFd);
        }
    }

    // Write the Elems
    // ===============
    // Write the quantity of elems
    tmp = _recElems.size();
    fwrite(&tmp, 4, 1, _recFd);
    // Loop on elems
    plgBegin(REC, "Elems");
    for(int elemIdx=0; elemIdx<_recElems.size(); ++elemIdx) {
        const auto& elem = _recElems[elemIdx];
        plgScope(REC, "Elem");
        if(elem.nameIdx!=PL_INVALID) plgData(REC, "Name", _recStrings[elem.nameIdx].value.toChar());
        plgData(REC, "ID", elemIdx);
        plgVar(REC, elem.threadBitmap, elem.hashPath, elem.prevElemIdx, elem.threadId, elem.nameIdx, elem.hlNameIdx, elem.flags,
               elem.isPartOfHStruct, elem.nestingLevel);

        // Write some elem information
        fwrite(&elem.hashPath,        8, 1, _recFd);
        fwrite(&elem.partialHashPath, 8, 1, _recFd);
        fwrite(&elem.threadBitmap,    8, 1, _recFd);
        fwrite(&elem.hashKey,         4, 1, _recFd);
        fwrite(&elem.prevElemIdx,     4, 1, _recFd);
        fwrite(&elem.threadId,        4, 1, _recFd);
        fwrite(&elem.nestingLevel,    4, 1, _recFd);
        fwrite(&elem.nameIdx,         4, 1, _recFd);
        fwrite(&elem.hlNameIdx,       4, 1, _recFd);
        fwrite(&elem.flags,           4, 1, _recFd);
        fwrite(&elem.isPartOfHStruct, 4, 1, _recFd);
        fwrite(&elem.isThreadHashed,  4, 1, _recFd);
        fwrite(&elem.absYMin,         8, 1, _recFd);
        fwrite(&elem.absYMax,         8, 1, _recFd);

        // Write the chunk indexes for this elem
        tmp = elem.chunkLocs.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "Elem chunk", tmp);
        if(tmp) fwrite(&elem.chunkLocs[0], sizeof(chunkLoc_t), tmp, _recFd);

        // Write the MR elem
        tmp = elem.mrSpeckChunks.size();
        fwrite(&tmp, 4, 1, _recFd);
        plgData(REC, "MR levels", tmp);

        // Loop on resolution levels
        for(const bsVec<cmRecord::ElemMR>& entries : elem.mrSpeckChunks) {
            plgScope(REC, "MR level");
            tmp = entries.size();
            fwrite(&tmp, 4, 1, _recFd);
            plgData(REC, "size", tmp);
            fwrite(&entries[0], sizeof(cmRecord::ElemMR), tmp, _recFd);
        } // End of loop on multi-resolution levels
    } // End of loop on elems
    plgEnd(REC, "Elems");

    // Save errors
    plgData(REC, "Error qty", _recErrorQty);
    fwrite(&_recErrorQty, 4, 1, _recFd);
    if(_recErrorQty) fwrite(&_recErrors[0], sizeof(cmRecord::RecError), _recErrorQty, _recFd);

    // Save the bootstrap indirection
    char magic[9] = "PL-MAGIC";
    fwrite(&magic[0], 1, 8, _recFd); // "Magic" identifier, to check that the file is indeed a Palanteer record
    fwrite(&headerStartOffset, 8, 1, _recFd); // File offset of the meta information start

    // Cleaning
    _recordAppName.clear();
    _recThreads.clear();
    _recStrings.clear();
    fclose(_recFd); _recFd = 0;
}


void
cmRecording::createDeltaRecord(cmRecord::Delta* delta)
{
    plScope("createDeltaRecord");
    plAssert(delta);

    // Flush the event data file so that reading from file with the newly added locations
    //  and with another file handler will succeed
    fflush(_recFd);

    // Statistics
    delta->durationNs     = _recDurationNs;
    delta->recordByteQty  = _recLastEventFileOffset;
    delta->coreQty        = _recCoreQty;
    delta->elemEventQty   = _recElemEventQty;
    delta->memEventQty    = _recMemEventQty;
    delta->ctxSwitchEventQty = _recCtxSwitchEventQty;
    delta->lockEventQty   = _recLockEventQty;
    delta->markerEventQty = _recMarkerEventQty;

    // Errors
    delta->errorQty = _recErrorQty-_recLastIdxErrorQty;
    if(delta->errorQty) {
        memcpy(&delta->errors[0], &_recErrors[_recLastIdxErrorQty],
               delta->errorQty*sizeof(cmRecord::RecError));
        _recLastIdxErrorQty = _recErrorQty;
    }

    // New streams
    for(int i=delta->streams.size(); i<_recStreams.size(); ++i) {
        delta->streams.push_back(_recStreams[i]);
    }

    // New strings
    delta->strings.resize(_recStrings.size()-_recLastSizeStrings);
    if(!delta->strings.empty()) {
        for(int i=_recLastSizeStrings; i<_recStrings.size(); ++i) {
            delta->strings[i-_recLastSizeStrings] = _recStrings[i];
        }
        _recLastSizeStrings = _recStrings.size();
    }

    // Updated strings (threadBitmapAsName field)
    delta->updatedStrings.clear();
    if(!_recUpdatedStringIds.empty()) {
        delta->updatedStrings.reserve(_recUpdatedStringIds.size());
        for(int stringId : _recUpdatedStringIds) {
            cmRecord::String& src = _recStrings[stringId];
            delta->updatedStrings.push_back({stringId, src.threadBitmapAsName, src.lockId, src.categoryId});
            src.isHexa = false; // Reset the change flag
        }
        _recUpdatedStringIds.clear();
    }

    // Marker categories
    for(int i=delta->markerCategories.size(); i<_recMarkerCategoryNameIdxs.size(); ++i) {
        delta->markerCategories.push_back(_recMarkerCategoryNameIdxs[i]);
    }

    // New locks
    for(int i=delta->locks.size(); i<_recLocks.size(); ++i) {
        delta->locks.push_back({_recLocks[i].nameIdx, {}});
    }

    // Update locks
    delta->updatedLockIds.clear();
    if(!_recUpdatedLockIds.empty()) {
        for(int lockId : _recUpdatedLockIds) {
            // Update the waiting thread ID list
            delta->locks[lockId].waitingThreadIds.reserve(_recLocks[lockId].waitingThreadIds.size());
            for(int i=delta->locks[lockId].waitingThreadIds.size(); i<_recLocks[lockId].waitingThreadIds.size(); ++i) {
                delta->locks[lockId].waitingThreadIds.push_back(_recLocks[lockId].waitingThreadIds[i]);
            }
        }
        delta->updatedLockIds = _recUpdatedLockIds;
        _recUpdatedLockIds.clear();
    }

    // New threads. Groups will be extracted when delta is applied
    for(int i=delta->threads.size(); i<_recThreads.size(); ++i) {
        delta->threads.push_back({});
        cmRecord::Thread&  dst = delta->threads.back();
        const ThreadBuild& src = _recThreads[i];
        dst.threadHash         = src.threadHash;
        dst.threadUniqueHash   = src.threadUniqueHash;
        dst.nameIdx            = src.nameIdx;
        dst.streamId           = src.streamId;
    }

    // Thread names updated?
    delta->updatedThreadIds.clear();
    if(!_recNameUpdatedThreadIds.empty()) {
        for(int tId : _recNameUpdatedThreadIds) {
            delta->threads[tId].nameIdx          = _recThreads[tId].nameIdx;
            delta->threads[tId].threadUniqueHash = _recThreads[tId].threadUniqueHash;
        }
        delta->updatedThreadIds = _recNameUpdatedThreadIds;
        _recNameUpdatedThreadIds.clear();
    }

#define UPDATE_FROM_RECORDING(s, d, name)                               \
    d.name##ChunkLocs.resize(s.name##ChunkLocs.size()-s.name##LastLocIdx); \
    if(!d.name##ChunkLocs.empty()) {                                    \
        memcpy(&d.name##ChunkLocs[0], &s.name##ChunkLocs[s.name##LastLocIdx], d.name##ChunkLocs.size()*sizeof(chunkLoc_t)); \
        s.name##LastLocIdx = s.name##ChunkLocs.size();                  \
    }                                                                   \
    if(!d.name##ChunkLocs.empty() || d.name##LastLiveEvtChunk.size()!=s.name##ChunkData.size()) { \
        d.name##LastLiveEvtChunk.resize(s.name##ChunkData.size());      \
        if(!d.name##LastLiveEvtChunk.empty()) {                         \
            memcpy(&d.name##LastLiveEvtChunk[0], &s.name##ChunkData[0], s.name##ChunkData.size()*sizeof(cmRecord::Evt)); \
        }                                                               \
    }

    // Update threads
    plAssert(delta->threads.size()==_recThreads.size());
    for(int i=0; i<_recThreads.size(); ++i) {
        ThreadBuild&      src = _recThreads[i];
        cmRecord::Thread& dst = delta->threads[i];

        // Stats
        dst.durationNs   = src.durationNs;
        dst.elemEventQty = src.elemEventQty;
        dst.memEventQty  = src.memEventQty;
        dst.ctxSwitchEventQty = src.ctxSwitchEventQty;
        dst.lockEventQty = src.lockEventQty;
        dst.markerEventQty = src.markerEventQty;

        // Update levels
        for(int j=dst.levels.size(); j<src.levels.size(); ++j) dst.levels.push_back({}); // New levels
        for(int j=0; j<src.levels.size(); ++j) {
            NestingLevelBuild&      lsrc = src.levels[j];
            cmRecord::NestingLevel& ldst = dst.levels[j];

            // Scope and non scope chunks
            UPDATE_FROM_RECORDING(lsrc, ldst, nonScope);
            UPDATE_FROM_RECORDING(lsrc, ldst, scope);

            // MR levels
            for(int k=ldst.mrScopeSpeckChunks.size(); k<lsrc.mrScopeSpeckChunks.size(); ++k) {  // New MR levels
                lsrc.lastMrScopeSpeckChunksIndexes.push_back(0);
                ldst.mrScopeSpeckChunks.push_back({});
            }
            plAssert(ldst.mrScopeSpeckChunks.size()==lsrc.mrScopeSpeckChunks.size(), ldst.mrScopeSpeckChunks.size(), lsrc.mrScopeSpeckChunks.size());
            for(int k=0; k<lsrc.mrScopeSpeckChunks.size(); ++k) {
                const bsVec<u32>& lmsrc = lsrc.mrScopeSpeckChunks[k];
                bsVec<u32>&       lmdst = ldst.mrScopeSpeckChunks[k];
                lmdst.resize(lmsrc.size()-lsrc.lastMrScopeSpeckChunksIndexes[k]);
                if(!lmdst.empty()) {
                    memcpy(&lmdst[0], &lmsrc[lsrc.lastMrScopeSpeckChunksIndexes[k]], lmdst.size()*sizeof(u32));
                    lsrc.lastMrScopeSpeckChunksIndexes[k] = lmsrc.size();
                }
            }
        } // End of loop on thread levels

        UPDATE_FROM_RECORDING(src, dst, memAlloc);
        UPDATE_FROM_RECORDING(src, dst, memDealloc);
        UPDATE_FROM_RECORDING(src, dst, memPlot);
        UPDATE_FROM_RECORDING(src, dst, ctxSwitch);
        UPDATE_FROM_RECORDING(src, dst, softIrq);
        UPDATE_FROM_RECORDING(src, dst, lockWait);

        // Update memory specific storage (only delta are copied)
        dst.memDeallocMIdx.resize(src.memDeallocMIdx.size()-src.memDeallocMIdxLastIdx);
        if(!dst.memDeallocMIdx.empty()) {
            memcpy(&dst.memDeallocMIdx[0], &src.memDeallocMIdx[src.memDeallocMIdxLastIdx], dst.memDeallocMIdx.size()*sizeof(u32));
            src.memDeallocMIdxLastIdx = src.memDeallocMIdx.size();
        }
        dst.memSnapshotIndexes.resize(src.memSnapshotIndexes.size()-src.memSnapshotIndexesLastIdx);
        if(!dst.memSnapshotIndexes.empty()) {
            memcpy(&dst.memSnapshotIndexes[0], &src.memSnapshotIndexes[src.memSnapshotIndexesLastIdx], dst.memSnapshotIndexes.size()*sizeof(cmRecord::MemSnapshot));
            src.memSnapshotIndexesLastIdx = src.memSnapshotIndexes.size();
        }
    } // End of loop on threads

    UPDATE_FROM_RECORDING(_recGlobal, (*delta), lockUse);
    UPDATE_FROM_RECORDING(_recGlobal, (*delta), lockNtf);
    UPDATE_FROM_RECORDING(_recGlobal, (*delta), coreUsage);
    UPDATE_FROM_RECORDING(_recGlobal, (*delta), marker);

    // New elems
    for(int i=delta->elems.size(); i<_recElems.size(); ++i) {
        const ElemBuild& src = _recElems[i];
        delta->elems.push_back({src.hashPath, src.partialHashPath, src.threadBitmap, src.hashKey, src.prevElemIdx, src.threadId, src.nestingLevel,
                src.nameIdx, src.hlNameIdx, src.flags, src.isPartOfHStruct, src.isThreadHashed, src.absYMin, src.absYMax});
    }

    // Update elems
    for(int elemId : _recUpdatedElemIds) {
        ElemBuild&      src = _recElems[elemId];
        cmRecord::Elem& dst = delta->elems[elemId];
        dst.threadBitmap = src.threadBitmap;
        dst.absYMin      = src.absYMin;
        dst.absYMax      = src.absYMax;
        src.hasDeltaChanges = false;

        // Location chunks (additional indirection for elems)
        dst.chunkLocs.resize(src.chunkLocs.size()-src.lastLocIdx);
        if(!dst.chunkLocs.empty()) {
            memcpy(&dst.chunkLocs[0], &src.chunkLocs[src.lastLocIdx], dst.chunkLocs.size()*sizeof(chunkLoc_t));
            src.lastLocIdx = src.chunkLocs.size();
        }
        if(!dst.chunkLocs.empty() || dst.lastLiveLocChunk.size()!=src.chunkLIdx.size()) {
            dst.lastLiveLocChunk.resize(src.chunkLIdx.size());
            if(!dst.lastLiveLocChunk.empty()) {
                memcpy(&dst.lastLiveLocChunk[0], &src.chunkLIdx[0], src.chunkLIdx.size()*sizeof(u32));
            }
        }

        // MR levels
        for(int k=dst.mrSpeckChunks.size(); k<src.mrSpeckChunks.size(); ++k) {  // New MR levels
            src.lastMrSpeckChunksIndexes.push_back(0);
            dst.mrSpeckChunks.push_back({});
        }
        plAssert(dst.mrSpeckChunks.size()==src.mrSpeckChunks.size());
        for(int k=0; k<src.mrSpeckChunks.size(); ++k) {
            const bsVec<cmRecord::ElemMR>& msrc = src.mrSpeckChunks[k];
            bsVec<cmRecord::ElemMR>&       mdst = dst.mrSpeckChunks[k];
            mdst.resize(msrc.size()-src.lastMrSpeckChunksIndexes[k]);
            if(!mdst.empty()) {
                memcpy(&mdst[0], &msrc[src.lastMrSpeckChunksIndexes[k]], mdst.size()*sizeof(cmRecord::ElemMR));
                src.lastMrSpeckChunksIndexes[k] = msrc.size();
            }
        }
    }
    delta->updatedElemIds = _recUpdatedElemIds;
    _recUpdatedElemIds.clear();
}
