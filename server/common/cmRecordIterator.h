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

#include "cmRecord.h"

// ===========================================
// Iterators on the record data
// ===========================================

class cmRecordIteratorScope {
public:
    cmRecordIteratorScope(const cmRecord* record, int threadId, int nestingLevel, s64 timeNs, double nsPerPix);
    cmRecordIteratorScope(const cmRecord* record, int threadId, int nestingLevel, u32 lIdx);

    // If isCoarse==true, use only scopeStartTimeNs&scopeEndTimeNs, else e&durationNs
    u32  getNextScope(bool& isCoarse, s64& scopeStartTimeNs, s64& scopeEndTimeNs, cmRecord::Evt& e, s64& durationNs);
    void getChildren(u32 firstChildLIdx, u32 parentLIdx, bool onlyScopes, bool onlyAttributes, bool doCmlyChildrenLimitQty,
                     bsVec<cmRecord::Evt>& dataChildren, bsVec<u32>& lIdxChildren);
    bool wasAScopeChildSeen(void) const { return _childScopeZoneSeen; } // Valid only after getChildren() call
    int   getThreadId(void)         const { return _threadId; }
    int   getNestingLevel(void)     const { return _nestingLevel; }
    void* getUniqueId(u32 scopeLIdx) const { return (void*)((u64)_threadId | (((u64)_nestingLevel)<<8) | (((u64)scopeLIdx)<<16)); }

private:
    const cmRecord* _record;
    int _threadId;
    int _nestingLevel;
    u32 _speckUs;
    int _mrLevel;
    u32 _lIdx;
    bool _childScopeZoneSeen;
};


// This iterator requires an associated threads and the scope/non-scope attribute,
//   which is not the case of the "simple plot" iterator below
class cmRecordIteratorElem {
public:
    cmRecordIteratorElem(void) { }
    cmRecordIteratorElem(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix);
    void init(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix);

    u32 getNextPoint(s64& timeNs, double& value, cmRecord::Evt& e);
    s64 getTimeRelativeIdx(int offset); // Works only for full res
private:
    const cmRecord* _record = 0;
    int _elemIdx = -1;
    int _threadId;
    int _nestingLevel;
    u32 _speckUs;
    int _mrLevel;
    u32 _plIdx;
};


// Not for scopes
class cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorTimePlotBase(void) = default;
    cmRecordIteratorTimePlotBase(const cmRecord* record, const bsVec<cmRecord::Evt>* lastLiveEvtChunk=0) :
        _record(record), _lastLiveEvtChunk(lastLiveEvtChunk), _elemIdx(-1), _speckUs(0), _mrLevel(-1), _pmIdx(0) { };
    void findLevelAndIdx(int elemIdx, s64 timeNs, double nsPerPix, const bsVec<chunkLoc_t>& chunkLocs);
    const cmRecord::Evt* getNextEvent(const bsVec<chunkLoc_t>& chunkLocs, bool& isCoarse, s64& timeNs, const cmRecord::Evt*& eCoarseEnd);
protected:
    const cmRecord* _record = 0;
    const bsVec<cmRecord::Evt>* _lastLiveEvtChunk;
    int _elemIdx = -1;
    u32 _speckUs;
    int _mrLevel;
    u32 _pmIdx;
};


class cmRecordIteratorMemStat : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorMemStat(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix);
    const cmRecord::Evt* getNextMemStat(void);
private:
    int _threadId;
};


class cmRecordIteratorMemScope {
public:
    cmRecordIteratorMemScope(const cmRecord* record, int threadId, s64 targetTimeNs, // Caution: targetTimeNs will always be after the real initialized time
                              bsVec<u32>* currentAllocMIdxs=0);                        // Optional. If null, no infos are stored

    bool getNextMemScope(cmRecord::Evt& e, u32& allocMIdx);
    bool getAllocEvent  (u32 allocMIdx, cmRecord::Evt& allocEvt  ); // Independent of current position
    bool getDeallocEvent(u32 allocMIdx, cmRecord::Evt& deallocEvt); // Independent of current position
private:
    const cmRecord* _record;
    int _threadId;
    u32 _mIdx;
};


class cmRecordIteratorCoreUsage : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorCoreUsage(const cmRecord* record, int coreId, s64 timeNs, double nsPerPix);
    // If isCoarse==true, use only timeNs&endTimeNs, else timeNs&(threadId | nameIdx)
    // NameIdx is valid only iff threadId==0xFF (else PL_INVALID)
    bool getNextSwitch(bool& isCoarse, s64& timeNs, s64& endTimeNs, int& threadId, u32& nameIdx);
};


class cmRecordIteratorCpuCurve : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorCpuCurve(const cmRecord* record, s64 timeNs, double nsPerPix);
    bool getNextPoint(s64& timeNs, int& usedCoreQty);
};


class cmRecordIteratorCtxSwitch : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorCtxSwitch(const cmRecord* record, int threadId, s64 timeNs, double nsPerPix);
    // If isCoarse==true, use only timeNs&endTimeNs, else timeNs&coreId
    bool getNextSwitch(bool& isCoarse, s64& timeNs, s64& endTimeNs, int& coreId);
private:
    int _threadId;
};


class cmRecordIteratorSoftIrq : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorSoftIrq(const cmRecord* record, int threadId, s64 timeNs, double nsPerPix);
    // If isCoarse==true, use only timeNs&endTimeNs, else timeNs&nameIdx
    bool getNextSwitch(bool& isCoarse, s64& timeNs, s64& endTimeNs, u32& nameIdx);
private:
    int _threadId;
};


class cmRecordIteratorMarker : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorMarker(void) = default;
    cmRecordIteratorMarker(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix);
    cmRecordIteratorMarker(const cmRecord* record, int threadId, u32 nameIdx, s64 timeNs, double nsPerPix);
    void init(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix);
    bool getNextMarker(bool& isCoarse, cmRecord::Evt& e);
    s64  getTimeRelativeIdx(int offset); // Works only for full res
};


class cmRecordIteratorLockWait : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorLockWait(const cmRecord* record, int threadId, s64 timeNs, double nsPerPix);
    // If isCoarse==true, use only timeNs&endTimeNs, else timeNs&nameId
    bool getNextLock(bool& isCoarse, s64& timeNs, s64& endTimeNs, cmRecord::Evt& e);
private:
    int _threadId;
};


class cmRecordIteratorLockNtf : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorLockNtf(void) = default;
    cmRecordIteratorLockNtf(const cmRecord* record, u32 nameIdx, s64 timeNs, double nsPerPix);
    void init(const cmRecord* record, u32 nameIdx, s64 timeNs, double nsPerPix);
    bool getNextLock(bool& isCoarse, cmRecord::Evt& e);
};


class cmRecordIteratorLockUse : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorLockUse(const cmRecord* record, u32 nameIdx, s64 timeNs, double nsPerPix);
    // If isCoarse==true, use only timeNs&endTimeNs, else timeNs&nameId
    bool getNextLock(bool& isCoarse, s64& timeNs, s64& endTimeNs, cmRecord::Evt& e);
};


class cmRecordIteratorLockUseGraph : public cmRecordIteratorTimePlotBase {
public:
    cmRecordIteratorLockUseGraph(void) = default;
    cmRecordIteratorLockUseGraph(const cmRecord* record, int threadId, u32 nameIdx, s64 timeNs, double nsPerPix);
    void init(const cmRecord* record, int threadId, u32 nameIdx, s64 timeNs, double nsPerPix);
    bool getNextLock(s64& timeNs, double& value, cmRecord::Evt& e);
};


class cmRecordIteratorHierarchy {
public:
    cmRecordIteratorHierarchy(const cmRecord* record, int threadId, int nestingLevel, u32 lIdx);

    struct Parent { cmRecord::Evt evt; u32 lIdx; };
    void getParents(bsVec<Parent>& parents);
    u64  getParentDurationNs(void);
    bool getItem(int& nestingLevel, u32& lIdx, cmRecord::Evt& evt, s64& scopeEndTimeNs);
    bool next(bool doSkipChildren, cmRecord::Evt& nextEvt);
    void rewindOneItem(bool doSkipChildren);

    int   getNestingLevel(void) const { return _nestingLevel; }
    u32   getLIdx(void)         const { return _lIdx; }
private:
    bool next_(bool doSkipChildren, cmRecord::Evt& nextEvt);
    const cmRecord* _record;
    int _threadId;
    int _nestingLevel;
    u32 _lIdx;
};


void cmGetRecordPosition(const cmRecord* record, int threadId, s64 targetTimeNs, int& outNestingLevel, u32& outLIdx);
