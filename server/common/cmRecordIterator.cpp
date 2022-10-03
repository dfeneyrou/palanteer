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

// This file implements some iterators on a record in order to read its different parts

#include "bsOs.h"
#include "cmConst.h"
#include "cmRecord.h"
#include "cmRecordIterator.h"

#ifndef PL_GROUP_ITZ
#define PL_GROUP_ITZ 0
#endif
#ifndef PL_GROUP_ITCHILD
#define PL_GROUP_ITCHILD 0
#endif
#ifndef PL_GROUP_ITPARENT
#define PL_GROUP_ITPARENT 0
#endif
#ifndef PL_GROUP_ITTEXT
#define PL_GROUP_ITTEXT 0
#endif
#ifndef PL_GROUP_ITREWIND
#define PL_GROUP_ITREWIND 0
#endif
#ifndef PL_GROUP_ITSCROLL
#define PL_GROUP_ITSCROLL 0
#endif
#ifndef PL_GROUP_ITELEM
#define PL_GROUP_ITELEM 0
#endif
#ifndef PL_GROUP_ITMEM
#define PL_GROUP_ITMEM 0
#endif
#ifndef PL_GROUP_ITSPB
#define PL_GROUP_ITSPB 0
#endif
#ifndef PL_GROUP_ITCS
#define PL_GROUP_ITCS 0
#endif
#ifndef PL_GROUP_ITLOCK
#define PL_GROUP_ITLOCK 0
#endif
#ifndef PL_GROUP_ITLOG
#define PL_GROUP_ITLOG 0
#endif


#define GET_LIDX(n)   ((n)&0x7FFFFFFF)
#define GET_ISFLAT(n) (((u32)(n))>>31)


// ===============================
// Scope iterator (for timeline)
// ===============================

cmRecordIteratorScope::cmRecordIteratorScope(const cmRecord* record, int threadId, int nestingLevel,
                                           s64 timeNs, double nsPerPix) :
    _record(record), _threadId(threadId), _nestingLevel(nestingLevel)
{
    plgScope(ITZ, "cmRecordIteratorScope::cmRecordIteratorScope");
    plgVar(ITZ, threadId, nestingLevel, timeNs);

    // Find the top level time
    plAssert(_threadId<_record->threads.size());
    const cmRecord::Thread& rt = _record->threads[_threadId];
    plAssert(_nestingLevel<rt.levels.size());
    const bsVec<bsVec<u32>>& mrScopeSpeckChunk = rt.levels[_nestingLevel].mrScopeSpeckChunks;
    _mrLevel = mrScopeSpeckChunk.size();
    if(_mrLevel==0) {
        plgText(ITZ, "IterScope", "No scope at this level");
        _mrLevel = -1;
        _lIdx = 0;
        return;
    }

    // Store the target speck size
    _speckUs = (u32)bsMin((s64)(nsPerPix/1024.), (s64)0xFFFFFFFF);
    plgVar(ITZ, _speckUs);

    // Top down navigation
    u64 mrLevelFactor = 1; for(int i=0; i<=_mrLevel; ++i) mrLevelFactor *= cmMRScopeSize;
    _lIdx  = 0;
    while(_mrLevel==mrScopeSpeckChunk.size() || (_mrLevel>0 && (int)_lIdx<mrScopeSpeckChunk[_mrLevel].size() &&
                                            mrScopeSpeckChunk[_mrLevel][_lIdx]>=_speckUs)) {
        // Go down a MR level
        const bsVec<u32>& entries = mrScopeSpeckChunk[--_mrLevel];
        _lIdx *= cmMRScopeSize;
        mrLevelFactor /= cmMRScopeSize;

        // Find our chunk, the last one which start time is after "timeNs"
        const bsVec<chunkLoc_t>& chunkLocs = rt.levels[_nestingLevel].scopeChunkLocs;
        const bsVec<cmRecord::Evt>* lastLiveEvtChunk = &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
        while((int)_lIdx<entries.size()) {
            u64 lIdx = mrLevelFactor*_lIdx;
            int mrIdx = (int)(lIdx/cmChunkSize);
            int eIdx  = lIdx%cmChunkSize;
            if(mrIdx>=chunkLocs.size()) break;
            const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(chunkLocs[mrIdx], lastLiveEvtChunk);
            if(eIdx>=chunkData.size()) break;
            if(chunkData[eIdx].vS64>=timeNs) break;
            ++_lIdx;
        }
        _lIdx = bsMax(0, (int)_lIdx-1);
        plgData(ITZ, "Current speck size ##µs", entries[_lIdx]);
        plgVar(ITZ, _mrLevel);
    }

    // Maybe one more level down (negative 1) to go to full resolution
    if(_mrLevel==0 && (int)_lIdx<mrScopeSpeckChunk[0].size() && mrScopeSpeckChunk[0][_lIdx]>=_speckUs) {
        _mrLevel = -1;
        _lIdx *= cmMRScopeSize;
        plgData(ITZ, "Start FR lIdx", _lIdx);
    } else {
        plgData(ITZ, "Start MR level", _mrLevel);
        plgData(ITZ, "Start MR lIdx", _lIdx);
        if((int)_lIdx<mrScopeSpeckChunk[_mrLevel].size()) {
            plgData(ITZ, "Current speck size ##µs", mrScopeSpeckChunk[_mrLevel][_lIdx]);
        }
    }
}


cmRecordIteratorScope::cmRecordIteratorScope(const cmRecord* record, int threadId, int nestingLevel, u32 lIdx) :
    _record(record), _threadId(threadId), _nestingLevel(nestingLevel), _speckUs(0), _mrLevel(-1), _lIdx(lIdx), _childScopeZoneSeen(false)
{
    plgScope(ITZ, "cmRecordIteratorScope::cmRecordIteratorScope");
    plgVar(ITZ, threadId, _nestingLevel, _lIdx);
}


void
cmRecordIteratorScope::getChildren(u32 firstChildLIdx, u32 parentLIdx, bool onlyScopes, bool onlyAttributes, bool doCmlyChildrenLimitQty,
                                    bsVec<cmRecord::Evt>& dataChildren, bsVec<u32>& lIdxChildren)
{
    // Get main infos
    plgScope(ITCHILD, "cmRecordIteratorScope::getChildren");
    _childScopeZoneSeen = false;
    dataChildren.clear();
    lIdxChildren.clear();
    plAssert(_threadId<_record->threads.size());
    const cmRecord::Thread& rt = _record->threads[_threadId];
    plgVar(ITZ, _threadId, _nestingLevel, GET_LIDX(firstChildLIdx), GET_ISFLAT(firstChildLIdx), parentLIdx);
    if(_nestingLevel+1>=rt.levels.size()) {
        plgText(ITCHILD, "IterScope", "No next level");
        return; // No children possible
    }
    const bsVec<chunkLoc_t>& nonScopeChunkLocs = rt.levels[_nestingLevel+1].nonScopeChunkLocs;
    const bsVec<chunkLoc_t>& scopeChunkLocs    = rt.levels[_nestingLevel+1].scopeChunkLocs;
    const bsVec<chunkLoc_t>* chunkLocs        = GET_ISFLAT(firstChildLIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
    if(chunkLocs->empty()) { plgText(ITCHILD, "IterScope", "Empty next level"); return; }

    u32 lIdx = firstChildLIdx;
    while(1) {
        // Get index of the potentiel child
        int mrIdx = GET_LIDX(lIdx)/cmChunkSize;
        int eIdx  = GET_LIDX(lIdx)%cmChunkSize;
        plgVar(ITCHILD, mrIdx, eIdx);
        chunkLocs = GET_ISFLAT(lIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
        const bsVec<cmRecord::Evt>* lastLiveEvtChunk = GET_ISFLAT(lIdx)? &rt.levels[_nestingLevel+1].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel+1].scopeLastLiveEvtChunk;
        if(mrIdx>=chunkLocs->size()) return;
        const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk((*chunkLocs)[mrIdx], lastLiveEvtChunk);
        if(eIdx>=chunkData.size()) return;
        const cmRecord::Evt& e1 = chunkData[eIdx];

        // Is is a child (compare both parentLIdx)?
        if(e1.parentLIdx!=parentLIdx) { plgData(ITCHILD, "Parent not matching", e1.parentLIdx); return; } // No more children

        // Some filtering
        int  eType   = e1.flags&PL_FLAG_TYPE_MASK;
        bool doStore = !onlyScopes && !onlyAttributes;
        if(!doStore) {
            bool isScope = (e1.flags&PL_FLAG_SCOPE_MASK) || eType==PL_FLAG_TYPE_ALLOC || eType==PL_FLAG_TYPE_DEALLOC;
            doStore = (onlyScopes && isScope) || (onlyAttributes && !isScope);
        }

        // Storage
        if(doStore) {
            plgData(ITCHILD, "Add child##hexa", e1.flags);
            dataChildren.push_back(e1);
            lIdxChildren.push_back(lIdx);
            if(doCmlyChildrenLimitQty && dataChildren.size()>=cmConst::CHILDREN_MAX) return;
        }
        if(e1.flags&PL_FLAG_TYPE_MASK) _childScopeZoneSeen = true;

        // Go to potential next child (next item for "begin" else the index in field linkLIdx)
        lIdx = (e1.flags&PL_FLAG_SCOPE_BEGIN)? (lIdx+1) : e1.linkLIdx;
    }
}


u32
cmRecordIteratorScope::getNextScope(bool& isCoarse, s64& scopeStartTimeNs, s64& scopeEndTimeNs, cmRecord::Evt& evt, s64& durationNs)
{
    // Get base fields
    plgScope(ITZ, "cmRecordIteratorScope::getNextScope");
    plAssert(_threadId<_record->threads.size());
    const cmRecord::Thread& rt = _record->threads[_threadId];
    plAssert(_nestingLevel<rt.levels.size());
    const bsVec<chunkLoc_t>& chunkLocs                = rt.levels[_nestingLevel].scopeChunkLocs;
    const bsVec<bsVec<u32>>& mrScopeSpeckChunk        = rt.levels[_nestingLevel].mrScopeSpeckChunks;
    const bsVec<cmRecord::Evt>* scopeLastLiveEvtChunk = &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
    plAssert(_mrLevel>=-1 && _mrLevel<mrScopeSpeckChunk.size(), _mrLevel, mrScopeSpeckChunk.size());
    plgVar(ITZ, _speckUs, _nestingLevel, _mrLevel, _lIdx);

    if(!mrScopeSpeckChunk.empty()) {
        // Increase precision until range is accessible and speck size is reached
        bool hasMrChanged = false;
        while(_mrLevel>=0 && ((int)_lIdx>=mrScopeSpeckChunk[_mrLevel].size() || mrScopeSpeckChunk[_mrLevel][_lIdx]>_speckUs)) {
            plgData(ITZ, "Lower level due to too high speck size", (int)_lIdx>=mrScopeSpeckChunk[_mrLevel].size()? -1 : mrScopeSpeckChunk[_mrLevel][_lIdx]);
            --_mrLevel; _lIdx *= cmMRScopeSize;  hasMrChanged = true;
        }

        // Decrease precision as much as speck size allows it
        while(!hasMrChanged && _mrLevel+1<mrScopeSpeckChunk.size() && (int)_lIdx/cmMRScopeSize<mrScopeSpeckChunk[_mrLevel+1].size() &&
              mrScopeSpeckChunk[_mrLevel+1][_lIdx/cmMRScopeSize]<_speckUs) {
            ++_mrLevel; _lIdx /= cmMRScopeSize;
            plgData(ITZ, "Upper level with speck size", mrScopeSpeckChunk[_mrLevel][_lIdx]);
        }
    }

    isCoarse = (_mrLevel>=0);
    plgData(ITZ, "Final MR level", _mrLevel);
    plgData(ITZ, "Final speck size", (isCoarse && _lIdx<(u32)mrScopeSpeckChunk[_mrLevel].size())? mrScopeSpeckChunk[_mrLevel][_lIdx] : 0);
    u64 mrLevelFactor = 1; for(int i=0; i<=_mrLevel; ++i) mrLevelFactor *= cmMRScopeSize;

    // Get start time
    u64 beginFullLIdx = isCoarse? mrLevelFactor*_lIdx : _lIdx;
    if(beginFullLIdx/cmChunkSize>=(u32)chunkLocs.size()) { plgText(ITZ, "IterScope", "End of record (1)"); return PL_INVALID; }
    const bsVec<cmRecord::Evt>& chunkDataStart = _record->getEventChunk(chunkLocs[(int)(beginFullLIdx/cmChunkSize)], scopeLastLiveEvtChunk);
    int eIdx = beginFullLIdx%cmChunkSize;
    if(eIdx>=chunkDataStart.size()) { plgText(ITZ, "IterScope", "End of record (2)"); return PL_INVALID; }
    plgAssert(ITZ, (eIdx&1)==0, eIdx, chunkDataStart.size(), beginFullLIdx, _mrLevel, mrLevelFactor, _lIdx);
    plgAssert(ITZ, chunkDataStart[eIdx].flags&PL_FLAG_SCOPE_BEGIN);
    if(isCoarse) {
        scopeStartTimeNs = chunkDataStart[eIdx].vS64;
        plgData(ITZ, "scope start time ##ns", scopeStartTimeNs);
    }
    else {
        evt = chunkDataStart[eIdx];
        plgData(ITZ, "scope start time ##ns", evt.vS64);
    }

    if(isCoarse) {
        u64 maxLIdx = cmChunkSize*chunkLocs.size()-1;
        u64 endLIdx = bsMin(beginFullLIdx+mrLevelFactor-1, maxLIdx);
        const bsVec<cmRecord::Evt>& chunkDataEnd = _record->getEventChunk(chunkLocs[(int)(endLIdx/cmChunkSize)], scopeLastLiveEvtChunk);
        eIdx = endLIdx%cmChunkSize;
        if(eIdx<chunkDataEnd.size()) { // "Normal" case
            plgAssert(ITZ, chunkDataEnd[eIdx].flags&PL_FLAG_SCOPE_END);
            scopeEndTimeNs = chunkDataEnd[eIdx].vS64;
            ++_lIdx; // Go to next MR scope
        }
        else { // Case live display or last chunk of record
            // If the end is not yet present (unfinished MR pyramid), then decrease the level
            while(_mrLevel>=0 && (int)_lIdx>=mrScopeSpeckChunk[_mrLevel].size()) {
                plgText(ITZ, "IterScope", "Lower level due to live unfinished pyramid");
                --_mrLevel; _lIdx *= cmMRScopeSize; mrLevelFactor /= cmMRScopeSize;
            }
            bool isEndCoarse = (_mrLevel>=0);
            endLIdx = isEndCoarse? bsMin(beginFullLIdx+mrLevelFactor-1, maxLIdx) : beginFullLIdx+1;
            const bsVec<cmRecord::Evt>& chunkDataEnd2 = _record->getEventChunk(chunkLocs[(int)(endLIdx/cmChunkSize)], scopeLastLiveEvtChunk);
            eIdx = bsMin((int)(endLIdx%cmChunkSize), (int)chunkDataEnd2.size()-1);
            if(chunkDataEnd2[eIdx].flags&PL_FLAG_SCOPE_END) {
                scopeEndTimeNs = chunkDataEnd2[eIdx].vS64; // Case artefact due to the unfinished MR pyramid
            }
            else {
                scopeEndTimeNs = rt.durationNs; // Case "end" block is not yet present
            }
            if(isEndCoarse) ++_lIdx; // Go to next MR scope
            else _lIdx += 2; // Go to next full resolution scope (so bypass 1 start and 1 end)
        }
        plgData(ITZ, "scope end time ##ns", scopeEndTimeNs);
    }
    else {
        // Get end time
        u64 endLIdx = beginFullLIdx+1;
        plgAssert(ITZ, endLIdx/cmChunkSize<(u32)chunkLocs.size(), endLIdx, endLIdx/cmChunkSize, chunkLocs.size());
        const bsVec<cmRecord::Evt>& chunkDataEnd = _record->getEventChunk(chunkLocs[(int)(endLIdx/cmChunkSize)], scopeLastLiveEvtChunk);
        eIdx = endLIdx%cmChunkSize;
        if(eIdx<chunkDataEnd.size()) { // "Normal" case
            plgAssert(ITZ, chunkDataEnd[eIdx].flags&PL_FLAG_SCOPE_END);
            durationNs = chunkDataEnd[eIdx].vS64-evt.vS64;
        } else { // Live display case where the "end" is not yet present
            durationNs = rt.durationNs-evt.vS64;
        }
        plgData(ITZ, "scope duration ##ns", durationNs);
        _lIdx += 2; // Go to next full resolution scope (so bypass 1 start and 1 end)
    }

    // Return the "scope begin" full resolution lIdx
    return (u32)beginFullLIdx;
}



// ====================================================
// Elem iterator for plots and histograms from timeline
// ====================================================

cmRecordIteratorElem::cmRecordIteratorElem(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix)
{
    init(record, elemIdx, timeNs, nsPerPix); // This iterator may be re-initialized
}


void
cmRecordIteratorElem::init(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix)
{
    plgScope(ITELEM,  "cmRecordIteratorElem::cmRecordIteratorElem");
    _record  = record;
    _elemIdx = elemIdx;
    _plIdx   = 0;

    // Find the top level time
    plAssert(_elemIdx<_record->elems.size(), _elemIdx, _record->elems.size());
    const cmRecord::Elem& elem = record->elems[elemIdx];
    _threadId     = elem.threadId;
    _nestingLevel = elem.nestingLevel;
    plgVar(ITELEM, _threadId, elemIdx, timeNs);
    const cmRecord::Thread&  rt                         = record->threads[_threadId];
    const bsVec<chunkLoc_t>& elemChunkLocs              = elem.chunkLocs;
    const bsVec<u32>&        elemLastLiveLocChunk       = elem.lastLiveLocChunk;
    const bsVec<bsVec<cmRecord::ElemMR>>& mrSpeckChunks = elem.mrSpeckChunks;
    _mrLevel = mrSpeckChunks.size();
    if(_mrLevel==0) {
        plgText(ITELEM, "IterElem", "No data");
        _mrLevel = -1;
        return;
    }

    // Store the target speck size
    _speckUs = (u32)bsMin((s64)(nsPerPix/1024.), (s64)0xFFFFFFFF);
    plgVar(ITELEM, _speckUs);

    // Top down navigation
    u64 mrLevelFactor = 1; for(int i=0; i<=_mrLevel; ++i) mrLevelFactor *= cmMRElemSize;
    while(_mrLevel==mrSpeckChunks.size() || (_mrLevel>0 && (int)_plIdx<mrSpeckChunks[_mrLevel].size() &&
                                             mrSpeckChunks[_mrLevel][_plIdx].speckUs>=_speckUs)) {
        // Go down a MR level
        const bsVec<cmRecord::ElemMR>& mrcData = mrSpeckChunks[--_mrLevel];
        _plIdx *= cmMRElemSize;
        mrLevelFactor /= cmMRElemSize;

        // Find our chunk, the last one which start time is after "timeNs"
        while((int)_plIdx<mrcData.size()) {
            // Get the lIdx from this plIdx (i.e. get the Evt index from the elem index data (which is the event lIdx))
            u64 plIdx  = mrLevelFactor*_plIdx;
            int pmrIdx = (int)(plIdx/cmElemChunkSize);
            int peIdx  = plIdx%cmElemChunkSize;
            if(pmrIdx>=elemChunkLocs.size()) break;
            const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
            if(peIdx>=elemChunkData.size()) break;
            u32 lIdx = elemChunkData[peIdx];

            // Get the event
            int mrIdx = GET_LIDX(lIdx)/cmChunkSize;
            int eIdx  = GET_LIDX(lIdx)%cmChunkSize;
            const bsVec<chunkLoc_t>&    nonScopeChunkLocs = rt.levels[_nestingLevel].nonScopeChunkLocs;
            const bsVec<chunkLoc_t>&    scopeChunkLocs    = rt.levels[_nestingLevel].scopeChunkLocs;
            const bsVec<chunkLoc_t>*    chunkLocs        = GET_ISFLAT(lIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
            const bsVec<cmRecord::Evt>* lastLiveEvtChunk = GET_ISFLAT(lIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
            if(mrIdx>=chunkLocs->size()) break;
            const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk((*chunkLocs)[mrIdx], lastLiveEvtChunk);
            if(eIdx>=chunkData.size()) break;
            const cmRecord::Evt& evt = chunkData[eIdx];
            if(!GET_ISFLAT(lIdx)) { // Case the event is a scope, so its value is the time we are looking for
                if(evt.vS64>=timeNs) break;
            }
            else { // Case the event is a non-scope, so we need its parent (scope) to get the time we are looking for
                plgAssert(ITELEM, !GET_ISFLAT(evt.parentLIdx));
                plgAssert(ITELEM, _nestingLevel>0);
                mrIdx = GET_LIDX(evt.parentLIdx)/cmChunkSize;
                eIdx  = GET_LIDX(evt.parentLIdx)%cmChunkSize;
                const bsVec<chunkLoc_t>& pScopeChunkLocs = rt.levels[_nestingLevel-1].scopeChunkLocs;
                if(mrIdx>=pScopeChunkLocs.size()) break;
                const bsVec<cmRecord::Evt>& pChunkData = _record->getEventChunk(pScopeChunkLocs[mrIdx],
                                                                                &rt.levels[_nestingLevel-1].scopeLastLiveEvtChunk);
                if(eIdx>=pChunkData.size()) break;
                if(pChunkData[eIdx].vS64>=timeNs) break;
            }

            ++_plIdx;
        }
        _plIdx = bsMax(0, (int)_plIdx-1);
        plgData(ITELEM, "Current speck size ##µs", mrcData[_plIdx].speckUs);
        plgData(ITELEM, "Current MR level", _mrLevel);
    }

    // Maybe one more level down (negative 1) to go to full resolution
    if(_mrLevel==0 && (int)_plIdx<mrSpeckChunks[0].size() && mrSpeckChunks[0][_plIdx].speckUs>=_speckUs) {
        _mrLevel = -1;
        _plIdx *= cmMRElemSize;
        plgData(ITELEM, "Start FR plIdx", _plIdx);
    } else {
        plgData(ITELEM, "Start MR level", _mrLevel);
        plgData(ITELEM, "Start MR plIdx", _plIdx);
        if((int)_plIdx<mrSpeckChunks[_mrLevel].size()) {
            plgData(ITELEM, "Current speck size ##µs", mrSpeckChunks[_mrLevel][_plIdx].speckUs);
        }
    }
}


u32
cmRecordIteratorElem::getNextPoint(s64& timeNs, double& value, cmRecord::Evt& evt)
{
    // Get base fields
    plgScope(ITELEM, "cmRecordIteratorElem::getNextPoint");
    plAssert(_threadId<_record->threads.size());
    const cmRecord::Thread& rt = _record->threads[_threadId];
    plAssert(_nestingLevel<rt.levels.size());
    const cmRecord::Elem& elem = _record->elems[_elemIdx];
    const bsVec<chunkLoc_t>& elemChunkLocs = elem.chunkLocs;
    const bsVec<u32>& elemLastLiveLocChunk = elem.lastLiveLocChunk;
    const bsVec<bsVec<cmRecord::ElemMR>>& mrSpeckChunks = elem.mrSpeckChunks;
    plAssert(_mrLevel>=-1 && _mrLevel<mrSpeckChunks.size(), _mrLevel, mrSpeckChunks.size());
    plgVar(ITELEM, _speckUs, _nestingLevel, _mrLevel, _plIdx);

    if(!mrSpeckChunks.empty() && !(_mrLevel>=0 && (int)_plIdx>=mrSpeckChunks[_mrLevel].size())) {
        // Increase precision until speck size is reached
        bool hasMrChanged = false;
        while(_mrLevel>=0 && mrSpeckChunks[_mrLevel][_plIdx].speckUs>_speckUs) {
            plgData(ITELEM, "Lower level due to too high speck size", mrSpeckChunks[_mrLevel][_plIdx].speckUs);
            --_mrLevel; _plIdx *= cmMRElemSize; hasMrChanged = true;
        }

        // Decrease precision as much as speck size allows it
        while(!hasMrChanged && _mrLevel+1<mrSpeckChunks.size() && (int)_plIdx/cmMRElemSize<mrSpeckChunks[_mrLevel+1].size() &&
              mrSpeckChunks[_mrLevel+1][_plIdx/cmMRElemSize].speckUs<_speckUs) {
            ++_mrLevel; _plIdx /= cmMRElemSize;
            plgData(ITELEM, "Upper level with speck size", mrSpeckChunks[_mrLevel][_plIdx].speckUs);
        }
    }
    if(_mrLevel>=0 && (int)_plIdx>=mrSpeckChunks[_mrLevel].size()) return PL_INVALID;

    bool isCoarse = (_mrLevel>=0);
    plgData(ITELEM, "Final MR level", _mrLevel);
    plgData(ITELEM, "Final speck size", isCoarse? mrSpeckChunks[_mrLevel][_plIdx].speckUs : 0);

    // Get event LIdx
    u32 lIdx = PL_INVALID;
    if(isCoarse) { // Easy case: lIdx is directly inside the MR structure
        lIdx = mrSpeckChunks[_mrLevel][_plIdx].lIdx;
    } else {       // Hard way: get the lIdx from the full resolution elem data (which are arrays of event lIdx)
        int pmrIdx = _plIdx/cmElemChunkSize;
        int peIdx  = _plIdx%cmElemChunkSize;
        if(pmrIdx>=elemChunkLocs.size()) return PL_INVALID;
        const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
        if(peIdx>=elemChunkData.size()) return PL_INVALID;
        lIdx = elemChunkData[peIdx];
    }

    // Get the event
    int mrIdx = GET_LIDX(lIdx)/cmChunkSize;
    int eIdx  = GET_LIDX(lIdx)%cmChunkSize;
    const bsVec<chunkLoc_t>&    nonScopeChunkLocs = rt.levels[_nestingLevel].nonScopeChunkLocs;
    const bsVec<chunkLoc_t>&    scopeChunkLocs    = rt.levels[_nestingLevel].scopeChunkLocs;
    const bsVec<chunkLoc_t>*    chunkLocs        = GET_ISFLAT(lIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = GET_ISFLAT(lIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
    if(mrIdx>=chunkLocs->size()) return PL_INVALID;
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk((*chunkLocs)[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size()) return PL_INVALID;
    evt = chunkData[eIdx];

    // Get the point time and value, according to the event type
    if(!GET_ISFLAT(lIdx)) {
        // Case the event is a scope:
        //  - the point time is the event time
        //  - its value is the scope duration, so we need the next scope (end) to compute it
        mrIdx = GET_LIDX(lIdx+1)/cmChunkSize;
        eIdx  = GET_LIDX(lIdx+1)%cmChunkSize;
        if(mrIdx>=scopeChunkLocs.size()) return PL_INVALID;
        const bsVec<cmRecord::Evt>& nchunkData = _record->getEventChunk(scopeChunkLocs[mrIdx], &rt.levels[_nestingLevel].scopeLastLiveEvtChunk);
        if(eIdx>=nchunkData.size()) return PL_INVALID;
        // Point output
        timeNs = evt.vS64;
        value  = (double)(nchunkData[eIdx].vS64-evt.vS64);
    }
    else {
        // Case the event is a non-scope:
        //  - we need its parent (scope) to get the time
        //  - its value is the value of the event
        plgAssert(ITELEM, !GET_ISFLAT(evt.parentLIdx));
        plgAssert(ITELEM, _nestingLevel>0);
        mrIdx = GET_LIDX(evt.parentLIdx)/cmChunkSize;
        eIdx  = GET_LIDX(evt.parentLIdx)%cmChunkSize;
        const bsVec<chunkLoc_t>& pScopeChunkLocs = rt.levels[_nestingLevel-1].scopeChunkLocs;
        if(mrIdx>=pScopeChunkLocs.size()) return PL_INVALID;
        const bsVec<cmRecord::Evt>& pChunkData = _record->getEventChunk(pScopeChunkLocs[mrIdx], &rt.levels[_nestingLevel-1].scopeLastLiveEvtChunk);
        if(eIdx>=pChunkData.size()) return PL_INVALID;
        // Point output
        timeNs = pChunkData[eIdx].vS64;
        switch(evt.flags&PL_FLAG_TYPE_MASK) {
        case PL_FLAG_TYPE_DATA_S32:    value = (double)evt.vInt; break;
        case PL_FLAG_TYPE_DATA_U32:    value = (double)evt.vU32; break;
        case PL_FLAG_TYPE_DATA_S64:    value = (double)evt.vS64; break;
        case PL_FLAG_TYPE_DATA_U64:    value = (double)evt.vU64; break;
        case PL_FLAG_TYPE_DATA_FLOAT:  value = (double)evt.vFloat; break;
        case PL_FLAG_TYPE_DATA_DOUBLE: value = (double)evt.vDouble; break;
        case PL_FLAG_TYPE_DATA_STRING: value = (double)evt.vStringIdx; break;
        case PL_FLAG_TYPE_LOCK_NOTIFIED: timeNs = evt.vS64; value = evt.nameIdx; break;
        default: plAssert(0, "bug, unknown type...", evt.flags);
        }
    }

    // Next point
    ++_plIdx;

    // Return the event lIdx
    return lIdx;
}


s64
cmRecordIteratorElem::getTimeRelativeIdx(int offset)
{
    // Get base fields
    plgScope(ITELEM, "cmRecordIteratorElem::getTimeRelativeIdx");
    plAssert(_mrLevel==-1); // Works only for full resolution
    plAssert(_threadId<_record->threads.size(), _threadId, _record->threads.size());
    const cmRecord::Thread& rt = _record->threads[_threadId];
    plAssert(_nestingLevel<rt.levels.size());
    const cmRecord::Elem& elem = _record->elems[_elemIdx];
    const bsVec<chunkLoc_t>& elemChunkLocs = elem.chunkLocs;
    const bsVec<u32>& elemLastLiveLocChunk = elem.lastLiveLocChunk;
    const bsVec<bsVec<cmRecord::ElemMR>>& mrSpeckChunks = elem.mrSpeckChunks;
    int plIdx = _plIdx+offset;
    if(mrSpeckChunks.empty() || plIdx<0) { plgText(ITELEM, "IterElem", "End of record (1)"); return -1; }

    // Get event LIdx: get the lIdx from the full resolution elem data (which are arrays of event lIdx)
    int pmrIdx = plIdx/cmElemChunkSize;
    int peIdx  = plIdx%cmElemChunkSize;
    if(pmrIdx>=elemChunkLocs.size()) return -1;
    const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
    if(peIdx>=elemChunkData.size()) return -1;
    u32 lIdx = elemChunkData[peIdx];

    // Get the event
    int mrIdx = GET_LIDX(lIdx)/cmChunkSize;
    int eIdx  = GET_LIDX(lIdx)%cmChunkSize;
    const bsVec<chunkLoc_t>& nonScopeChunkLocs   = rt.levels[_nestingLevel].nonScopeChunkLocs;
    const bsVec<chunkLoc_t>& scopeChunkLocs      = rt.levels[_nestingLevel].scopeChunkLocs;
    const bsVec<chunkLoc_t>* chunkLocs           = GET_ISFLAT(lIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = GET_ISFLAT(lIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
    if(mrIdx>=chunkLocs->size()) return -1;
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk((*chunkLocs)[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size()) return -1;

    // Get the point time according to the event type
    if(!GET_ISFLAT(lIdx)) {
        // Case the event is a scope: the point time is the event time
        return chunkData[eIdx].vS64;
    }
    else {
        // Case the event is a non-scope: we need its parent (scope) to get the time
        cmRecord::Evt evt = chunkData[eIdx];
        plgAssert(ITELEM, !GET_ISFLAT(evt.parentLIdx));
        plgAssert(ITELEM, _nestingLevel>0);
        mrIdx = GET_LIDX(evt.parentLIdx)/cmChunkSize;
        eIdx  = GET_LIDX(evt.parentLIdx)%cmChunkSize;
        const bsVec<chunkLoc_t>& pScopeChunkLocs = rt.levels[_nestingLevel-1].scopeChunkLocs;
        if(mrIdx>=pScopeChunkLocs.size()) return PL_INVALID;
        const bsVec<cmRecord::Evt>& pChunkData = _record->getEventChunk(pScopeChunkLocs[mrIdx], &rt.levels[_nestingLevel-1].scopeLastLiveEvtChunk);
        if(eIdx>=pChunkData.size()) return PL_INVALID;
        return pChunkData[eIdx].vS64;
    }
}


// ===============================
// Memory statistic iterator
// ===============================

cmRecordIteratorMemStat::cmRecordIteratorMemStat(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record), _threadId(-1)
{
    plgScope(ITMEM,  "cmRecordIteratorMemStat::cmRecordIteratorMemStat");
    const cmRecord::Elem& elem = record->elems[elemIdx];
    _threadId = elem.threadId;
    plgVar(ITMEM, _threadId, elemIdx);
    findLevelAndIdx(elemIdx, timeNs, nsPerPix, _record->threads[_threadId].memPlotChunkLocs);
}

// Note: we choose not to use cmRecordIteratorTimePlotBase::getNextEvent because we do not need the coarse block size (so simpler)
const cmRecord::Evt*
cmRecordIteratorMemStat::getNextMemStat(void)
{
    // Get base fields
    plgScope(ITMEM, "cmRecordIteratorMemStat::getNextMemStat");
    const bsVec<chunkLoc_t>& chunkLocs     = _record->threads[_threadId].memPlotChunkLocs;
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = &_record->threads[_threadId].memPlotLastLiveEvtChunk;
    const cmRecord::Elem&    elem          = _record->elems[_elemIdx];
    const bsVec<chunkLoc_t>& elemChunkLocs = elem.chunkLocs;
    const bsVec<u32>& elemLastLiveLocChunk = elem.lastLiveLocChunk;
    const bsVec<bsVec<cmRecord::ElemMR>>& mrSpeckChunks = elem.mrSpeckChunks;
    plgAssert(ITMEM, _mrLevel>=-1 && _mrLevel<mrSpeckChunks.size(), _mrLevel, mrSpeckChunks.size());
    if(mrSpeckChunks.empty() || (_mrLevel>=0 && (int)_pmIdx>=mrSpeckChunks[_mrLevel].size())) {
        plgVar(ITMEM, mrSpeckChunks.empty(), _mrLevel, _pmIdx, mrSpeckChunks[_mrLevel].size());
        plgText(ITMEM, "IterMem", "End of record (1)"); return 0;
    }
    plgVar(ITMEM, _speckUs, _mrLevel, _pmIdx);

    // Increase precision until speck size is reached
    bool hasMrChanged = false;
    while(_mrLevel>=0 && mrSpeckChunks[_mrLevel][_pmIdx].speckUs>_speckUs) {
        plgData(ITMEM, "Lower level due to too high speck size", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
        --_mrLevel; _pmIdx *= cmMRElemSize; hasMrChanged = true;
    }

    // Decrease precision as much as speck size allows it
    while(!hasMrChanged && _mrLevel+1<mrSpeckChunks.size() && (int)_pmIdx/cmMRElemSize<mrSpeckChunks[_mrLevel+1].size() &&
          mrSpeckChunks[_mrLevel+1][_pmIdx/cmMRElemSize].speckUs<_speckUs) {
        ++_mrLevel; _pmIdx /= cmMRElemSize;
        plgData(ITMEM, "Upper level with speck size", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
    }

    bool isCoarse = (_mrLevel>=0);
    plgData(ITMEM, "Final MR level", _mrLevel);
    plgData(ITMEM, "Final speck size", isCoarse? mrSpeckChunks[_mrLevel][_pmIdx].speckUs : 0);

    // Get event mIdx
    u32 mIdx = PL_INVALID;
    if(isCoarse) { // Easy case: mIdx is directly inside the MR structure
        mIdx = mrSpeckChunks[_mrLevel][_pmIdx].lIdx;
    } else {       // Hard way: get the mIdx from the full resolution Elem data (which are not event but arrays of event mIdx)
        int pmrIdx = _pmIdx/cmElemChunkSize;
        int peIdx  = _pmIdx%cmElemChunkSize;
        if(pmrIdx>=elemChunkLocs.size()) { plgText(ITMEM, "IterMem", "elem data chunk out of bound"); return 0; }
        const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
        if(peIdx>=elemChunkData.size())  { plgText(ITMEM, "IterMem", "elem data index out of bound"); return 0; }
        mIdx = elemChunkData[peIdx];
    }

    // Get the point time and value from the event
    int mrIdx = mIdx/cmChunkSize;
    int eIdx  = mIdx%cmChunkSize;
    if(mrIdx>=chunkLocs.size()) { plgText(ITMEM, "IterMem", "chunk index out of bound"); return 0; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(chunkLocs[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size())  { plgText(ITMEM, "IterMem", "data index out of bound"); return 0; }

    ++_pmIdx; // Next point next time
    return &chunkData[eIdx];
}


// ===============================
// Memory event iterator
// ===============================

cmRecordIteratorMemScope::cmRecordIteratorMemScope(const cmRecord* record, int threadId, s64 timeNs, bsVec<u32>* currentAllocMIdxs) :
    _record(record), _threadId(threadId), _mIdx(0)
{
    plgScope(ITMEM, "cmRecordIteratorMemScope::cmRecordIteratorMemScope");
    plgVar(ITMEM, threadId, timeNs);
    plAssert(_threadId<_record->threads.size());
    if(currentAllocMIdxs) {
        plgText(ITMEM, "IterMem", "Clearing the current allocation array");
        currentAllocMIdxs->clear();
    }

    // Find the snapshot @#OPTIM Replace the linear search with binary search
    const bsVec<cmRecord::MemSnapshot>& memSnapshotIndexes = _record->threads[_threadId].memSnapshotIndexes;
    int snapshotIdx = 0;
    while(snapshotIdx<memSnapshotIndexes.size() && memSnapshotIndexes[snapshotIdx].timeNs<=timeNs) ++snapshotIdx;
    --snapshotIdx;
    _mIdx = (snapshotIdx>=0)? memSnapshotIndexes[snapshotIdx].allocMIdx : 0;
    plgVar(ITMEM, snapshotIdx, memSnapshotIndexes.size(), _mIdx);

    // Fill the scope array if present, with the content of the snapshot
    if(snapshotIdx>=0 && currentAllocMIdxs) {
        _record->getMemorySnapshot(_threadId, snapshotIdx, *currentAllocMIdxs);
    }
}


bool
cmRecordIteratorMemScope::getNextMemScope(cmRecord::Evt& e, u32& allocMIdx)
{
    plgScope(ITMEM, "cmRecordIteratorMemScope::getNextMemScope");
    plgData(ITMEM, "mIdx",     _mIdx&0x7FFFFFFF);
    plgData(ITMEM, "isAlloc", (_mIdx&0x80000000)==0);
    if(_mIdx==PL_INVALID) { plgText(ITMEM, "IterMem", "Invalid mIdx"); return false; }
    const cmRecord::Thread& rt = _record->threads[_threadId];
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = &rt.memAllocLastLiveEvtChunk;

    // Get the event
    int mrIdx = (_mIdx&0x7FFFFFFF)/cmChunkSize;
    int eIdx  = (_mIdx&0x7FFFFFFF)%cmChunkSize;
    if(_mIdx&0x80000000) {
        // Deallocation case
        if(mrIdx>=rt.memDeallocChunkLocs.size()) { plgText(ITMEM, "IterMem", "dealloc chunk index out of bound"); _mIdx = PL_INVALID; return false; }
        const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(rt.memDeallocChunkLocs[mrIdx], lastLiveEvtChunk);
        if(eIdx>=chunkData.size()) { plgText(ITMEM, "IterMem", "dealloc data index out of bound"); _mIdx = PL_INVALID; return false; }
        e = chunkData[eIdx];
        allocMIdx = e.allocSizeOrMIdx;
    }
    else {
        // Allocation case
        if(mrIdx>=rt.memAllocChunkLocs.size()) { plgText(ITMEM, "IterMem", "alloc chunk index out of bound"); _mIdx = PL_INVALID; return false; }
        const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(rt.memAllocChunkLocs[mrIdx], lastLiveEvtChunk);
        if(eIdx>=chunkData.size()) { plgText(ITMEM, "IterMem", "alloc data index out of bound"); _mIdx = PL_INVALID; return false; }
        e = chunkData[eIdx];
        allocMIdx = _mIdx;
    }

    // Next mIdx
    plgAssert(ITMEM, e.memLinkIdx!=_mIdx, _mIdx, e.flags);
    _mIdx = e.memLinkIdx;
    plgData(ITMEM, "next mIdx", _mIdx);
    return true;
}


bool
cmRecordIteratorMemScope::getAllocEvent(u32 allocMIdx, cmRecord::Evt& allocEvt)
{
    plgScope(ITMEM, "cmRecordIteratorMemScope::getAllocEvent");
    plgVar(ITMEM, allocMIdx);
    // Get the alloc event pointed by allocMIdx
    const cmRecord::Thread& rt = _record->threads[_threadId];
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = &rt.memAllocLastLiveEvtChunk;
    int mrIdx = allocMIdx/cmChunkSize;
    int eIdx  = allocMIdx%cmChunkSize;
    if(mrIdx>=rt.memAllocChunkLocs.size()) { plgText(ITMEM, "IterMem", "chunk index out of bound"); return false; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(rt.memAllocChunkLocs[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size()) { plgText(ITMEM, "IterMem", "data index out of bound"); return false; }
    allocEvt = chunkData[eIdx];
    return true;
}


bool
cmRecordIteratorMemScope::getDeallocEvent(u32 allocMIdx, cmRecord::Evt& deallocEvt)
{
    plgScope(ITMEM, "cmRecordIteratorMemScope::getDeallocEvent");
    plgVar(ITMEM, allocMIdx);
    // Get the deallocMIdx from the allocMIdx
    const cmRecord::Thread& rt = _record->threads[_threadId];
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = &rt.memDeallocLastLiveEvtChunk;
    if(allocMIdx>=(u32)rt.memDeallocMIdx.size()) { plgText(ITMEM, "IterMem", "allocMIdx out of lookup bound"); return false; }
    u32 deallocMIdx = rt.memDeallocMIdx[allocMIdx];
    if(deallocMIdx==PL_INVALID) { plgText(ITMEM, "IterMem", "Leaked"); return false; }
    plgVar(ITMEM, deallocMIdx);
    // Get the dealloc event pointed by the deallocMIdx
    int mrIdx = deallocMIdx/cmChunkSize;
    int eIdx  = deallocMIdx%cmChunkSize;
    if(mrIdx>=rt.memDeallocChunkLocs.size()) { plgText(ITMEM, "IterMem", "chunk index out of bound"); return false; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(rt.memDeallocChunkLocs[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size()) { plgText(ITMEM, "IterMem", "data index out of bound"); return false; }
    deallocEvt = chunkData[eIdx];
    return true;
}



// ======================================
// Simple Plot base iterator
// ======================================

// This iterator requires uniform chunks (not the case for tree events) and time as value
// It is the case for most non-tree events (ctx switch, core usage, lock usage, lock notif, etc...)
void
cmRecordIteratorTimePlotBase::findLevelAndIdx(int elemIdx, s64 timeNs, double nsPerPix, const bsVec<chunkLoc_t>& chunkLocs)
{
    plgScope (ITSPB, "findLevelAndIdx");
    plgVar(ITSPB,  timeNs, elemIdx);

    // Find the top level time
    _elemIdx = elemIdx;
    const cmRecord::Elem& elem = _record->elems[_elemIdx];
    const bsVec<chunkLoc_t>& elemChunkLocs = elem.chunkLocs;
    const bsVec<u32>& elemLastLiveLocChunk = elem.lastLiveLocChunk;
    const bsVec<bsVec<cmRecord::ElemMR>>& mrSpeckChunks = elem.mrSpeckChunks;
    _mrLevel = mrSpeckChunks.size();
    if(_mrLevel==0) {
        plgText(ITSPB, "IterPlot", "No data");
        _mrLevel = -1;
        return;
    }

    // Store the target speck size
    _speckUs = (u32)bsMin((s64)(nsPerPix/1024.), (s64)0xFFFFFFFF);
    plgVar(ITSPB, _speckUs);

    // Top down navigation
    u64 mrLevelFactor = 1; for(int i=0; i<=_mrLevel; ++i) mrLevelFactor *= cmMRElemSize;
    while(_mrLevel==mrSpeckChunks.size() || (_mrLevel>0 && (int)_pmIdx<mrSpeckChunks[_mrLevel].size() &&
                                             mrSpeckChunks[_mrLevel][_pmIdx].speckUs>=_speckUs)) {
        // Go down a MR level
        const bsVec<cmRecord::ElemMR>& mrcData = mrSpeckChunks[--_mrLevel];
        _pmIdx *= cmMRElemSize;
        mrLevelFactor /= cmMRElemSize;

        // Find our chunk, the last one which start time is after "timeNs"
        while((int)_pmIdx<mrcData.size()) {
            // Get the mIdx from this pmIdx (i.e. get the Evt index from the elem index data (which is the event mIdx))
            u64 pmIdx  = mrLevelFactor*_pmIdx;
            int pmrIdx = (int)(pmIdx/cmElemChunkSize);
            int peIdx  = pmIdx%cmElemChunkSize;
            if(pmrIdx>=elemChunkLocs.size()) break;
            const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
            if(peIdx>=elemChunkData.size()) break;
            u32 mIdx = elemChunkData[peIdx];

            // Get the event
            int mrIdx = mIdx/cmChunkSize;
            int eIdx  = mIdx%cmChunkSize;
            if(mrIdx>=chunkLocs.size()) break;
            const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(chunkLocs[mrIdx], _lastLiveEvtChunk);
            if(eIdx>=chunkData.size()) break;
            const cmRecord::Evt& evt = chunkData[eIdx];
            if(evt.vS64>=timeNs) break; // The time is the value of the event (common for all "plotbase-able" events)
            ++_pmIdx;
        }
        _pmIdx = bsMax(0, (int)_pmIdx-1);
        plgData(ITSPB, "Current speck size ##µs", mrcData[_pmIdx].speckUs);
        plgData(ITSPB, "Current MR level", _mrLevel);
    }

    // Maybe one more level down (negative 1) to go to full resolution
    if(_mrLevel==0 && (int)_pmIdx<mrSpeckChunks[0].size() && mrSpeckChunks[0][_pmIdx].speckUs>=_speckUs) {
        _mrLevel = -1;
        _pmIdx *= cmMRElemSize;
        plgData(ITSPB, "Start FR pmIdx", _pmIdx);
    } else {
        plgData(ITSPB, "Start MR level", _mrLevel);
        plgData(ITSPB, "Start MR pmIdx", _pmIdx);
        if((int)_pmIdx<mrSpeckChunks[_mrLevel].size()) {
            plgData(ITSPB, "Current speck size ##µs", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
        }
    }
}


const cmRecord::Evt*
cmRecordIteratorTimePlotBase::getNextEvent(const bsVec<chunkLoc_t>& chunkLocs, bool& isCoarse, s64& timeNs, const cmRecord::Evt*& eCoarseEnd)
{
    // Get base fields
    plgScope(ITSPB, "cmRecordIteratorTimePlotBase::getNextEvent");
    if(_elemIdx<0) return 0;
    const cmRecord::Elem&   elem           = _record->elems[_elemIdx];
    const bsVec<chunkLoc_t>& elemChunkLocs = elem.chunkLocs;
    const bsVec<u32>& elemLastLiveLocChunk = elem.lastLiveLocChunk;
    const bsVec<bsVec<cmRecord::ElemMR>>& mrSpeckChunks = elem.mrSpeckChunks;
    plgAssert(ITSPB, _mrLevel>=-1 && _mrLevel<mrSpeckChunks.size(), _mrLevel, mrSpeckChunks.size());
    if((mrSpeckChunks.empty() && elemLastLiveLocChunk.empty()) || (_mrLevel>=0 && (int)_pmIdx>=mrSpeckChunks[_mrLevel].size())) {
        plgVar(ITSPB, mrSpeckChunks.empty(), _mrLevel, mrSpeckChunks.size(), _pmIdx);
        if(_mrLevel>=0) plgVar(ITSPB, mrSpeckChunks[_mrLevel].size());
        plgText(ITSPB, "IterPlot", "End of record"); return 0;
    }
    plgVar(ITSPB, _speckUs, _mrLevel, _pmIdx);

    // Increase precision until speck size is reached
    bool hasMrChanged = false;
    while(_mrLevel>=0 && mrSpeckChunks[_mrLevel][_pmIdx].speckUs>_speckUs) {
        plgData(ITSPB, "Lower level due to too high speck size", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
        --_mrLevel; _pmIdx *= cmMRElemSize; hasMrChanged = true;
    }

    // Decrease precision as much as speck size allows it
    while(!hasMrChanged && _mrLevel+1<mrSpeckChunks.size() && (int)_pmIdx/cmMRElemSize<mrSpeckChunks[_mrLevel+1].size() &&
          mrSpeckChunks[_mrLevel+1][_pmIdx/cmMRElemSize].speckUs<_speckUs) {
        ++_mrLevel; _pmIdx /= cmMRElemSize;
        plgData(ITSPB, "Upper level with speck size", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
    }

    isCoarse = (_mrLevel>=0);
    plgData(ITSPB, "Final MR level", _mrLevel);
    plgData(ITSPB, "Final speck size", isCoarse? mrSpeckChunks[_mrLevel][_pmIdx].speckUs : 0);
    u64 mrLevelFactor = 1; for(int i=0; i<=_mrLevel; ++i) mrLevelFactor *= cmMRElemSize;

    // Get the mIdx from the full resolution Elem data (which are not event but arrays of event mIdx)
    u32 mIdx = PL_INVALID;
    if(isCoarse) { // Easy case: mIdx is directly inside the MR structure (the maximum value)
        mIdx = mrSpeckChunks[_mrLevel][_pmIdx].lIdx;
    }
    else {         // Hard way: get the mIdx from the full resolution Elem data (which are not event but arrays of event mIdx)
        u64 frPmIdx = isCoarse? _pmIdx*mrLevelFactor : _pmIdx;
        int pmrIdx  = (int)(frPmIdx/cmElemChunkSize);
        int peIdx   = frPmIdx%cmElemChunkSize;
        if(pmrIdx>=elemChunkLocs.size()) { plgText(ITSPB, "IterPlot", "elem data chunk out of bound"); return 0; }
        const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
        if(peIdx>=elemChunkData.size())  { plgText(ITSPB, "IterPlot", "elem data index out of bound (1)"); return 0; }
        mIdx = elemChunkData[peIdx];
    }

    // Get the point time and value from the event
    int mrIdx = mIdx/cmChunkSize;
    int eIdx  = mIdx%cmChunkSize;
    if(mrIdx>=chunkLocs.size()) { plgText(ITSPB, "IterPlot", "chunk index out of bound"); return 0; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(chunkLocs[mrIdx], _lastLiveEvtChunk);
    if(eIdx>=chunkData.size())  { plgText(ITSPB, "IterPlot", "data index out of bound"); return 0; }

    timeNs = chunkData[eIdx].vS64;
    plgVar(ITSPB, timeNs);

    // If coarse, do the same with the end of the chunk in order to get the scope end date
    if(isCoarse) {
        // Get the mIdx from the full resolution Elem data (which are not event but arrays of event mIdx)
        u64 frPmIdx = _pmIdx*mrLevelFactor + mrLevelFactor-1;
        int pmrIdx  = (int)(frPmIdx/cmElemChunkSize);
        int peIdx   = frPmIdx%cmElemChunkSize;
        plgVar(ITSPB, pmrIdx);
        plgData(ITSPB, "max pmrIdx", elemChunkLocs.size()-1);
        if(pmrIdx>=elemChunkLocs.size()) { pmrIdx = elemChunkLocs.size()-1; peIdx = cmElemChunkSize-1; } // Last switch
        const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
        plgData(ITSPB, "peIdx", pmrIdx);
        plgData(ITSPB, "max peIdx", elemChunkData.size()-1);
        mIdx = elemChunkData[bsMin(peIdx, elemChunkData.size()-1)];

        // Get the point time and value from the event
        int mrIdx2 = mIdx/cmChunkSize;
        int eIdx2  = mIdx%cmChunkSize;
        if(mrIdx2>=chunkLocs.size()) { plgText(ITSPB, "IterPlot", "chunk index out of bound (2)"); return 0; }
        const bsVec<cmRecord::Evt>& chunkData2 = _record->getEventChunk(chunkLocs[mrIdx2], _lastLiveEvtChunk);
        if(eIdx2>=chunkData2.size())  { plgText(ITSPB, "IterPlot", "data index out of bound (2)"); return 0; }
        eCoarseEnd = &chunkData2[eIdx2];
   }

    ++_pmIdx; // Next point next time
    return &chunkData[eIdx]; // Valid until the cache line is purged
}


// ======================================
// Core usage iterator (for timeline)
// ======================================

cmRecordIteratorCoreUsage::cmRecordIteratorCoreUsage(const cmRecord* record, int coreId, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->coreUsageLastLiveEvtChunk))

{
    plgScope(ITCS, "cmRecordIteratorCoreUsage::cmRecordIteratorCoreUsage");
    plgVar(ITCS, coreId);
    plAssert(coreId<_record->coreQty);

    // Get the context switch plot elem for this thread
    int* elemIdxPtr = record->elemPathToId.find(bsHashStepChain(coreId, cmConst::CORE_USAGE_NAMEIDX), cmConst::CORE_USAGE_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->coreUsageChunkLocs);
}


bool
cmRecordIteratorCoreUsage::getNextSwitch(bool& isCoarse, s64& timeNs, s64& endTimeNs, int& threadId, u32& nameIdx)
{
    plgScope(ITCS, "cmRecordIteratorCoreUsage::getNextSwitch");
    const cmRecord::Evt* eCoarseEnd = 0;
    const cmRecord::Evt* e = getNextEvent(_record->coreUsageChunkLocs, isCoarse, timeNs, eCoarseEnd);
    if(!e) return false;

    if(isCoarse) {
        threadId  = eCoarseEnd->threadId;
        endTimeNs = eCoarseEnd->vS64;
        nameIdx   = eCoarseEnd->nameIdx;
    } else {
        threadId = e->threadId;
        nameIdx  = e->nameIdx;
    }
    plgVar(ITCS, threadId, timeNs);
    return true;
}


// ======================================
// CPU curve iterator (for timeline)
// ======================================

cmRecordIteratorCpuCurve::cmRecordIteratorCpuCurve(const cmRecord* record, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->coreUsageLastLiveEvtChunk))
{
    plgScope(ITCS,  "cmRecordIteratorCpuCurve::cmRecordIteratorCpuCurve");

    // Get the context switch plot elem for this thread
    int* elemIdxPtr = record->elemPathToId.find(bsHashStepChain(cmConst::CPU_CURVE_NAMEIDX), cmConst::CPU_CURVE_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->coreUsageChunkLocs);
}


bool
cmRecordIteratorCpuCurve::getNextPoint(s64& timeNs, int& usedCoreQty)
{
    plgScope(ITCS, "cmRecordIteratorCpuCurve::getNextPoint");
    bool isCoarse;
    const cmRecord::Evt* eCoarseEnd = 0;
    const cmRecord::Evt* e = getNextEvent(_record->coreUsageChunkLocs, isCoarse, timeNs, eCoarseEnd);
    if(!e) return false;

    usedCoreQty = e->usedCoreQty; // Multi-resolution scheme at recording time selects the highest point
    plgVar(ITCS, timeNs, usedCoreQty);
    return true;
}


// ======================================
// Context switch iterator (for timeline)
// ======================================

cmRecordIteratorCtxSwitch::cmRecordIteratorCtxSwitch(const cmRecord* record, int threadId, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->threads[threadId].ctxSwitchLastLiveEvtChunk)), _threadId(threadId)
{
    plgScope(ITCS,  "cmRecordIteratorCtxSwitch::cmRecordIteratorCtxSwitch");
    plgVar(ITCS, threadId);

    // Get the context switch plot elem for this thread.
    // Note that we use the "threadId" and not its hash name here, because no need for persistency across run for any config (none existing)
    //  and also ctx switch events would be dropped at the beginning of the record because they are sent before the thread declaration due to the
    //  double buffering mechanism in the client side (ctx switch events bypass this double buffering)
    int* elemIdxPtr = record->elemPathToId.find(bsHashStepChain(threadId, cmConst::CTX_SWITCH_NAMEIDX), cmConst::CTX_SWITCH_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->threads[_threadId].ctxSwitchChunkLocs);
}


bool
cmRecordIteratorCtxSwitch::getNextSwitch(bool& isCoarse, s64& timeNs, s64& endTimeNs, int& coreId)
{
    plgScope(ITCS, "cmRecordIteratorCtxSwitch::getNextSwitch");
    const cmRecord::Evt* eCoarseEnd = 0;
    const cmRecord::Evt* e = getNextEvent(_record->threads[_threadId].ctxSwitchChunkLocs, isCoarse, timeNs, eCoarseEnd);
    if(!e) return false;

    if(isCoarse) endTimeNs = eCoarseEnd->vS64;
    coreId = e->coreId;
    plgVar(ITCS, timeNs, coreId);
    return true;
}


// ======================================
// SOFTIRQ iterator (for timeline)
// ======================================

cmRecordIteratorSoftIrq::cmRecordIteratorSoftIrq(const cmRecord* record, int threadId, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->threads[threadId].softIrqLastLiveEvtChunk)), _threadId(threadId)
{
    plgScope(ITCS,  "cmRecordIteratorSoftIrq::cmRecordIteratorSoftIrq");
    plgVar(ITCS, threadId);

    // Same comment than for context switch in previous iterator
    int* elemIdxPtr = record->elemPathToId.find(bsHashStepChain(threadId, cmConst::SOFTIRQ_NAMEIDX), cmConst::SOFTIRQ_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->threads[_threadId].softIrqChunkLocs);
}


bool
cmRecordIteratorSoftIrq::getNextSwitch(bool& isCoarse, s64& timeNs, s64& endTimeNs, u32& nameIdx)
{
    plgScope(ITCS, "cmRecordIteratorSoftIrq::getNextSwitch");
    const cmRecord::Evt* eCoarseEnd = 0;
    const cmRecord::Evt* e = getNextEvent(_record->threads[_threadId].softIrqChunkLocs, isCoarse, timeNs, eCoarseEnd);
    if(!e) return false;

    if(isCoarse) endTimeNs = eCoarseEnd->vS64;
    nameIdx = (e->flags&PL_FLAG_SCOPE_BEGIN)? e->nameIdx : PL_INVALID;
    plgVar(ITCS, timeNs, nameIdx);
    return true;
}


// ======================================
// Log iterator
// ======================================

cmRecordIteratorLog::cmRecordIteratorLog(const cmRecord* record, int threadId, u32 nameIdx, int logLevel, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->logLastLiveEvtChunk))
{
    plAssert(threadId>=0);
    plAssert(logLevel>=0 && logLevel<=3);
    plgScope(ITLOG, "cmRecordIteratorLog::cmRecordIteratorLog");
    plgVar(ITLOG, threadId, logLogLevel);

    // Get the hashpath
    plAssert(nameIdx!=PL_INVALID);
    u64 hashPath = bsHashStepChain(record->threads[threadId].threadHash, logLevel, nameIdx, cmConst::LOG_NAMEIDX);

    // Get the log plot elem
    int* elemIdxPtr = record->elemPathToId.find(hashPath, cmConst::LOG_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->logChunkLocs);
}


cmRecordIteratorLog::cmRecordIteratorLog(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->logLastLiveEvtChunk))
{
    plgScope(ITLOG, "cmRecordIteratorLog::cmRecordIteratorLog");
    plgVar(ITLOG, elemIdx);
    findLevelAndIdx(elemIdx, timeNs, nsPerPix, _record->logChunkLocs);
}


void
cmRecordIteratorLog::init(const cmRecord* record, int elemIdx, s64 timeNs, double nsPerPix)
{
    plgScope(ITLOG, "cmRecordIteratorLog::init");
    plgVar(ITLOG, elemIdx);
    _record  = record;
    _lastLiveEvtChunk = &(record->logLastLiveEvtChunk);
    _pmIdx   = 0;
    findLevelAndIdx(elemIdx, timeNs, nsPerPix, _record->logChunkLocs);
}


bool
cmRecordIteratorLog::getNextLog(bool& isCoarse, cmRecord::Evt& eOut, bsVec<cmLogParam>& params)
{
    // Get base fields
    plgScope(ITLOG, "cmRecordIteratorLog::getNextLog");
    if(_elemIdx<0) return false;
    const bsVec<chunkLoc_t>& chunkLocs     = _record->logChunkLocs;
    const cmRecord::Elem&    elem          = _record->elems[_elemIdx];
    const bsVec<chunkLoc_t>& elemChunkLocs = elem.chunkLocs;
    const bsVec<u32>& elemLastLiveLocChunk = elem.lastLiveLocChunk;
    const bsVec<bsVec<cmRecord::ElemMR>>& mrSpeckChunks = elem.mrSpeckChunks;
    plgAssert(ITLOG, _mrLevel>=-1 && _mrLevel<mrSpeckChunks.size(), _mrLevel, mrSpeckChunks.size());
    if((mrSpeckChunks.empty() && elemLastLiveLocChunk.empty()) || (_mrLevel>=0 && (int)_pmIdx>=mrSpeckChunks[_mrLevel].size())) {
        plgVar(ITLOG, mrSpeckChunks.empty(), _mrLevel, mrSpeckChunks.size(), _pmIdx);
        if(_mrLevel>=0) plgVar(ITLOG, mrSpeckChunks[_mrLevel].size());
        plgText(ITLOG, "IterPlot", "End of record"); return false;
    }
    plgVar(ITLOG, _speckUs, _mrLevel, _pmIdx);

    // Increase precision until speck size is reached
    bool hasMrChanged = false;
    while(_mrLevel>=0 && mrSpeckChunks[_mrLevel][_pmIdx].speckUs>_speckUs) {
        plgData(ITLOG, "Lower level due to too high speck size", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
        --_mrLevel; _pmIdx *= cmMRElemSize; hasMrChanged = true;
    }

    // Decrease precision as much as speck size allows it
    while(!hasMrChanged && _mrLevel+1<mrSpeckChunks.size() && (int)_pmIdx/cmMRElemSize<mrSpeckChunks[_mrLevel+1].size() &&
          mrSpeckChunks[_mrLevel+1][_pmIdx/cmMRElemSize].speckUs<_speckUs) {
        ++_mrLevel; _pmIdx /= cmMRElemSize;
        plgData(ITLOG, "Upper level with speck size", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
    }

    isCoarse = (_mrLevel>=0);
    plgData(ITLOG, "Final MR level", _mrLevel);
    plgData(ITLOG, "Final speck size", isCoarse? mrSpeckChunks[_mrLevel][_pmIdx].speckUs : 0);
    u64 mrLevelFactor = 1; for(int i=0; i<=_mrLevel; ++i) mrLevelFactor *= cmMRElemSize;

    // Get the mIdx from the full resolution Elem data (which are not event but arrays of event mIdx)
    u32 mIdx = PL_INVALID;
    if(isCoarse) { // Easy case: mIdx is directly inside the MR structure (the maximum value)
        mIdx = mrSpeckChunks[_mrLevel][_pmIdx].lIdx;
    }
    else {         // Hard way: get the mIdx from the full resolution Elem data (which are not event but arrays of event mIdx)
        u64 frPmIdx = isCoarse? _pmIdx*mrLevelFactor : _pmIdx;
        int pmrIdx  = (int)(frPmIdx/cmElemChunkSize);
        int peIdx   = frPmIdx%cmElemChunkSize;
        if(pmrIdx>=elemChunkLocs.size()) { plgText(ITLOG, "IterPlot", "elem data chunk out of bound"); return false; }
        const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
        if(peIdx>=elemChunkData.size())  { plgText(ITLOG, "IterPlot", "elem data index out of bound (1)"); return false; }
        mIdx = elemChunkData[peIdx];
    }

    // Get the point time and value from the event
    int mrIdx = mIdx/cmChunkSize;
    int eIdx  = mIdx%cmChunkSize;
    if(mrIdx>=chunkLocs.size()) { plgText(ITLOG, "IterPlot", "chunk index out of bound"); return false; }
    const bsVec<cmRecord::Evt>* chunkData = &_record->getEventChunk(chunkLocs[mrIdx], _lastLiveEvtChunk);
    if(eIdx>=chunkData->size())  { plgText(ITLOG, "IterPlot", "data index out of bound"); return false; }
    eOut = (*chunkData)[eIdx];

    // Get the parameters. They are stored contiguously
    params.clear();
    if((eOut.lineNbr&0x8000)==0) {  // The 0x8000 bit means end of parameters
        // Loop on log param events
        while(1) {
            if(++eIdx>=chunkData->size()) {
                ++mrIdx; eIdx = 0;
                if(mrIdx>=chunkLocs.size()) { plgText(ITLOG, "IterPlot", "chunk index out of bound"); return false; }
                chunkData = &_record->getEventChunk(chunkLocs[mrIdx], _lastLiveEvtChunk);
                if(eIdx>=chunkData->size())  { plgText(ITLOG, "IterPlot", "data index out of bound"); return false; }
            }
            const cmRecord::Evt& paramEvt = (*chunkData)[eIdx];
            if(paramEvt.flags!=PL_FLAG_TYPE_LOG_PARAM) return false;  // Bad syntax
            // Loop on parameters inside this event
            int dataOffset = 4;  // Up to 24 bytes =sizeof(EventExt)
            u8* payload = (u8*)&(paramEvt.threadId);
            for(int paramIdx=0; paramIdx<5; ++paramIdx) {  // 5 parameters max in a log param event
                int paramType = (paramEvt.lineNbr>>(3*paramIdx))&0x7;
                if(paramType==0) break; // Last parameter in this event

                params.push_back({paramType, {0}});
                cmLogParam& p = params.back();
                switch(paramType) {
                case PL_FLAG_TYPE_DATA_S32:
                    if(dataOffset<=20) { p.vInt   = *( int32_t*)(payload+dataOffset); dataOffset += 4; }
                    else return false;
                    break;
                case PL_FLAG_TYPE_DATA_U32:
                    if(dataOffset<=20) { p.vU32   = *(uint32_t*)(payload+dataOffset); dataOffset += 4; }
                    else return false;
                    break;
                case PL_FLAG_TYPE_DATA_FLOAT:
                    if(dataOffset<=20) { p.vFloat = *(float*)   (payload+dataOffset); dataOffset += 4; }
                    else return false;
                    break;
                case PL_FLAG_TYPE_DATA_S64:
                    if(dataOffset<=16) { p.vS64   = *( int64_t*)(payload+dataOffset); dataOffset += 8; }
                    else return false;
                    break;
                case PL_FLAG_TYPE_DATA_U64:
                    if(dataOffset<=16) { p.vU64   = *(uint64_t*)(payload+dataOffset); dataOffset += 8; }
                    else return false;
                    break;
                case PL_FLAG_TYPE_DATA_DOUBLE:
                    if(dataOffset<=16) { p.vDouble = *(double*)(payload+dataOffset); dataOffset += 8; }
                    else return false;
                    break;
                case PL_FLAG_TYPE_DATA_STRING:
                    if(dataOffset<=16) { p.vStringIdx = *(uint32_t*)(payload+dataOffset); dataOffset += 8; }
                    else return false;
                    break;
                default:
                    return false;
                };
            }

            // Last log param event?
            if(paramEvt.lineNbr&0x8000) break;
        }
    }
    ++_pmIdx; // Next point next time
    return true;
}


s64
cmRecordIteratorLog::getTimeRelativeIdx(int offset)
{
    plgScope(ITLOG, "cmRecordIteratorLog::getTimeRelativeIdx");

    const cmRecord::Elem&    elem          = _record->elems[_elemIdx];
    const bsVec<chunkLoc_t>& elemChunkLocs = elem.chunkLocs;
    const bsVec<u32>& elemLastLiveLocChunk = elem.lastLiveLocChunk;
    if((int)_pmIdx+offset<0) { plgText(ITLOG, "IterLog", "End of record"); return -1; }

    // Get the index of the event from the plot index arrays (full resolution required)
    int pmrIdx = (_pmIdx+offset)/cmElemChunkSize;
    int peIdx  = (_pmIdx+offset)%cmElemChunkSize;
    if(pmrIdx>=elemChunkLocs.size()) { plgText(ITLOG, "IterLog", "elem data chunk out of bound"); return -1; }
    const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
    if(peIdx>=elemChunkData.size())  { plgText(ITLOG, "IterLog", "elem data index out of bound (1)"); return -1; }
    u32 mIdx = elemChunkData[peIdx];

    // Get the event
    int mrIdx = mIdx/cmChunkSize;
    int eIdx  = mIdx%cmChunkSize;
    if(mrIdx>=_record->logChunkLocs.size()) { plgText(ITLOG, "IterLog", "chunk index out of bound"); return -1; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(_record->logChunkLocs[mrIdx], _lastLiveEvtChunk);
    if(eIdx>=chunkData.size())  { plgText(ITLOG, "IterLog", "data index out of bound"); return -1; }
    return chunkData[eIdx].vS64;
}


// ======================================
// LockWait iterator (for timeline)
// ======================================

cmRecordIteratorLockWait::cmRecordIteratorLockWait(const cmRecord* record, int threadId, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->threads[threadId].lockWaitLastLiveEvtChunk)), _threadId(threadId)
{
    plgScope(ITLOCK,  "cmRecordIteratorLockWait::cmRecordIteratorLockWait");
    plgVar(ITLOCK, threadId);

    // Get the lock wait plot elem for this thread.
    int* elemIdxPtr = record->elemPathToId.find(bsHashStepChain(record->threads[threadId].threadHash, cmConst::LOCK_WAIT_NAMEIDX),
                                                cmConst::LOCK_WAIT_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->threads[_threadId].lockWaitChunkLocs);
}


bool
cmRecordIteratorLockWait::getNextLock(bool& isCoarse, s64& timeNs, s64& endTimeNs, cmRecord::Evt& eOut)
{
    plgScope(ITLOCK, "cmRecordIteratorLockWait::getNextLock");
    const cmRecord::Evt* eCoarseEnd = 0;
    const cmRecord::Evt* e = getNextEvent(_record->threads[_threadId].lockWaitChunkLocs, isCoarse, timeNs, eCoarseEnd);
    if(!e) return false;

    if(isCoarse) endTimeNs = eCoarseEnd->vS64;
    eOut = *e;
    plgVar(ITLOCK, timeNs);
    return true;
}


// ======================================
// LockUse iterator
// ======================================

// For timeline
cmRecordIteratorLockUse::cmRecordIteratorLockUse(const cmRecord* record, u32 nameIdx, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->lockUseLastLiveEvtChunk))
{
    plgScope(ITLOCK, "cmRecordIteratorLockUse::cmRecordIteratorLockUse");
    plgVar(ITLOCK, nameIdx);

    // Get the lock wait plot elem for this thread.
    int* elemIdxPtr = record->elemPathToId.find(bsHashStepChain(record->getString(nameIdx).hash, cmConst::LOCK_USE_NAMEIDX), cmConst::LOCK_USE_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->lockUseChunkLocs);
}


bool
cmRecordIteratorLockUse::getNextLock(bool& isCoarse, s64& timeNs, s64& endTimeNs, cmRecord::Evt& eOut)
{
    plgScope(ITLOCK, "cmRecordIteratorLockUse::getNextLock");
    const cmRecord::Evt* eCoarseEnd = 0;
    const cmRecord::Evt* e = getNextEvent(_record->lockUseChunkLocs, isCoarse, timeNs, eCoarseEnd);
    if(!e) return false;

    if(isCoarse) endTimeNs = eCoarseEnd->vS64;
    eOut = *e;
    plgVar(ITLOCK, timeNs);
    return true;
}


// For timeline
cmRecordIteratorLockUseGraph::cmRecordIteratorLockUseGraph(const cmRecord* record, int threadId, u32 nameIdx, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->lockUseLastLiveEvtChunk))
{
    init(record, threadId, nameIdx, timeNs, nsPerPix); // This iterator may be re-initialized
}


void
cmRecordIteratorLockUseGraph::init(const cmRecord* record, int threadId, u32 nameIdx, s64 timeNs, double nsPerPix)
{
    plgScope(ITLOCK, "cmRecordIteratorLockUseGraph::init");
    plgVar(ITLOCK, threadId, nameIdx);
    _record  = record;
    _lastLiveEvtChunk = &(record->lockUseLastLiveEvtChunk);
    _pmIdx   = 0;

    u64 hashPath;
    if(threadId>=0) {
        hashPath = bsHashStepChain(record->threads[threadId].threadHash, record->getString(nameIdx).hash, cmConst::LOCK_USE_NAMEIDX);
    } else  {
        hashPath = bsHashStepChain(record->getString(nameIdx).hash, cmConst::LOCK_USE_NAMEIDX);
    }
    int* elemIdxPtr = record->elemPathToId.find(hashPath, cmConst::LOCK_USE_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->lockUseChunkLocs);
}


bool
cmRecordIteratorLockUseGraph::getNextLock(s64& timeNs, double& value, cmRecord::Evt& evt) // @#QUESTION Is timeNs useful? It is the same as evt.vS64...
{
    // Get base fields
    plgScope(ITLOCK, "cmRecordIteratorTimePlotBase::getNextEvent");
    if(_elemIdx<0) return false;
    const bsVec<chunkLoc_t>& chunkLocs     = _record->lockUseChunkLocs;
    const cmRecord::Elem&   elem           = _record->elems[_elemIdx];
    const bsVec<chunkLoc_t>& elemChunkLocs = elem.chunkLocs;
    const bsVec<u32>& elemLastLiveLocChunk = elem.lastLiveLocChunk;
    const bsVec<bsVec<cmRecord::ElemMR>>& mrSpeckChunks = elem.mrSpeckChunks;
    plgAssert(ITLOCK, _mrLevel>=-1 && _mrLevel<mrSpeckChunks.size(), _mrLevel, mrSpeckChunks.size());
    if((mrSpeckChunks.empty() && elemLastLiveLocChunk.empty()) || (_mrLevel>=0 && (int)_pmIdx>=mrSpeckChunks[_mrLevel].size())) {
        plgVar(ITLOCK, mrSpeckChunks.empty(), _mrLevel, mrSpeckChunks.size(), _pmIdx);
        if(_mrLevel>=0) plgVar(ITLOCK, mrSpeckChunks[_mrLevel].size());
        plgText(ITLOCK, "IterLock", "End of record"); return false;
    }
    plgVar(ITLOCK, _speckUs, _mrLevel, _pmIdx);

    // Increase precision until speck size is reached
    bool hasMrChanged = false;
    while(_mrLevel>=0 && mrSpeckChunks[_mrLevel][_pmIdx].speckUs>_speckUs) {
        plgData(ITLOCK, "Lower level due to too high speck size", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
        --_mrLevel; _pmIdx *= cmMRElemSize; hasMrChanged = true;
    }

    // Decrease precision as much as speck size allows it
    while(!hasMrChanged && _mrLevel+1<mrSpeckChunks.size() && (int)_pmIdx/cmMRElemSize<mrSpeckChunks[_mrLevel+1].size() &&
          mrSpeckChunks[_mrLevel+1][_pmIdx/cmMRElemSize].speckUs<_speckUs) {
        ++_mrLevel; _pmIdx /= cmMRElemSize;
        plgData(ITLOCK, "Upper level with speck size", mrSpeckChunks[_mrLevel][_pmIdx].speckUs);
    }

    bool isCoarse = (_mrLevel>=0);
    plgData(ITLOCK, "Final MR level", _mrLevel);
    plgData(ITLOCK, "Final speck size", isCoarse? mrSpeckChunks[_mrLevel][_pmIdx].speckUs : 0);
    u64 mrLevelFactor = 1; for(int i=0; i<=_mrLevel; ++i) mrLevelFactor *= cmMRElemSize;

    // Get the mIdx from the full resolution Elem data (which are not event but arrays of event mIdx)
    u64 frPmIdx = isCoarse? _pmIdx*mrLevelFactor : _pmIdx;
    int pmrIdx  = (int)(frPmIdx/cmElemChunkSize);
    int peIdx   = frPmIdx%cmElemChunkSize;
    if(pmrIdx>=elemChunkLocs.size()) { plgText(ITLOCK, "IterLock", "elem data chunk out of bound"); return false; }
    const bsVec<u32>& elemChunkData = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
    if(peIdx>=elemChunkData.size())  { plgText(ITLOCK, "IterLock", "elem data index out of bound (1)"); return false; }
    u32 mIdx = elemChunkData[peIdx];

    // Get the point time from the event
    int mrIdx = mIdx/cmChunkSize;
    int eIdx  = mIdx%cmChunkSize;
    if(mrIdx>=chunkLocs.size()) { plgText(ITLOCK, "IterLock", "chunk index out of bound"); return false; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(chunkLocs[mrIdx], _lastLiveEvtChunk);
    if(eIdx>=chunkData.size())  { plgText(ITLOCK, "IterLock", "data index out of bound"); return false; }
    evt    = chunkData[eIdx];
    timeNs = evt.vS64;
    plgVar(ITLOCK, timeNs);

    // Get the end point, next event
    // Get the mIdx from the full resolution Elem data (which are not event but arrays of event mIdx)
    pmrIdx  = (int)((frPmIdx+1)/cmElemChunkSize);
    peIdx   = (frPmIdx+1)%cmElemChunkSize;
    if(pmrIdx>=elemChunkLocs.size()) { pmrIdx = elemChunkLocs.size()-1; peIdx = cmElemChunkSize-1; } // Last switch
    const bsVec<u32>& elemChunkData2 = _record->getElemChunk(elemChunkLocs[pmrIdx], &elemLastLiveLocChunk);
    mIdx = elemChunkData2[bsMin(peIdx, elemChunkData2.size()-1)];

    // Get the point time and value from the event
    int mrIdx2 = mIdx/cmChunkSize;
    int eIdx2  = mIdx%cmChunkSize;
    if(mrIdx2>=chunkLocs.size()) { plgText(ITLOCK, "IterLock", "chunk index out of bound (2)"); return false; }
    const bsVec<cmRecord::Evt>& chunkData2 = _record->getEventChunk(chunkLocs[mrIdx2], _lastLiveEvtChunk);
    if(eIdx2>=chunkData2.size())  { plgText(ITLOCK, "IterLock", "data index out of bound (2)"); return false; }
    value = (double)(chunkData2[eIdx2].vS64-timeNs);

    _pmIdx += isCoarse? 1:2; // Next point next time

    return true;
}


// ======================================
// LockNtf iterator (for timeline)
// ======================================

cmRecordIteratorLockNtf::cmRecordIteratorLockNtf(const cmRecord* record, u32 nameIdx, s64 timeNs, double nsPerPix) :
    cmRecordIteratorTimePlotBase(record, &(record->lockNtfLastLiveEvtChunk))
{
    init(record, nameIdx, timeNs, nsPerPix);
}

void
cmRecordIteratorLockNtf::init(const cmRecord* record, u32 nameIdx, s64 timeNs, double nsPerPix)
{
    plgScope(ITLOCK, "cmRecordIteratorLockUse::init");
    plgVar(ITLOCK, nameIdx);
    _record  = record;
    _lastLiveEvtChunk = &(record->lockNtfLastLiveEvtChunk);
    _pmIdx   = 0;

    // Get the lock wait plot elem for this thread.
    int* elemIdxPtr = record->elemPathToId.find(bsHashStepChain(record->getString(nameIdx).hash, cmConst::LOCK_NTF_NAMEIDX), cmConst::LOCK_NTF_NAMEIDX);
    if(elemIdxPtr) findLevelAndIdx(*elemIdxPtr, timeNs, nsPerPix, _record->lockNtfChunkLocs);
}


bool
cmRecordIteratorLockNtf::getNextLock(bool& isCoarse, cmRecord::Evt& eOut)
{
    plgScope(ITLOCK, "cmRecordIteratorLockNtf::getNextLock");
    const cmRecord::Evt* eCoarseEnd = 0;
    s64 timeNs = 0;
    const cmRecord::Evt* e = getNextEvent(_record->lockNtfChunkLocs, isCoarse, timeNs, eCoarseEnd);
    if(!e) return false;
    eOut = *e;
    plgVar(ITLOCK, timeNs);
    return true;
}


// ===============================
// Hierarchy iterator (for text and tooltips with children)
// ===============================

cmRecordIteratorHierarchy::cmRecordIteratorHierarchy(const cmRecord* record, int threadId, int nestingLevel, u32 lIdx) :
    _record(record), _threadId(threadId), _nestingLevel(nestingLevel), _lIdx(lIdx)
{
    plgScope(ITTEXT, "cmRecordIteratorHierarchy::cmRecordIteratorHierarchy");
    plgVar(ITTEXT, threadId, nestingLevel, lIdx);
}


void
cmRecordIteratorHierarchy::init(const cmRecord* record, int threadId, int nestingLevel, u32 lIdx)
{
    plgScope(ITTEXT, "cmRecordIteratorHierarchy::init");
    plgVar(ITTEXT, threadId, nestingLevel, lIdx);
    _record       = record;
    _threadId     = threadId;
    _nestingLevel = nestingLevel;
    _lIdx         = lIdx;
}


u64
cmRecordIteratorHierarchy::getParentDurationNs(void)
{
    plgScope(ITPARENT, "cmRecordIteratorHierarchy::getParentDurationNs");
    const cmRecord::Thread& rt = _record->threads[_threadId];
    plAssert(GET_ISFLAT(_lIdx));

    // Get current item
    if(_nestingLevel>=rt.levels.size())  { plgText(ITPARENT, "IterHierc", "Current out of nesting level"); return 0; }
    if(_nestingLevel==0)                 { plgText(ITPARENT, "IterHierc", "Top nesting level"); return 0; }
    const bsVec<chunkLoc_t>& nonScopeChunkLocs = rt.levels[_nestingLevel].nonScopeChunkLocs;
    int mrIdx = GET_LIDX(_lIdx)/cmChunkSize;
    int eIdx  = GET_LIDX(_lIdx)%cmChunkSize;
    if(mrIdx>=nonScopeChunkLocs.size()) { plgText(ITPARENT, "IterHierc", "Current out of chunk index"); return 0; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk(nonScopeChunkLocs[mrIdx], &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk);
    if(eIdx>=chunkData.size()) { plgText(ITPARENT, "IterHierc", "Current out of chunk data"); return 0; }
    cmRecord::Evt evt = chunkData[eIdx];

    // Get parent
    const bsVec<chunkLoc_t>& pscopeChunkLocs = rt.levels[_nestingLevel-1].scopeChunkLocs;
    mrIdx = GET_LIDX(evt.parentLIdx)/cmChunkSize;
    eIdx  = GET_LIDX(evt.parentLIdx)%cmChunkSize;
    if(mrIdx>=pscopeChunkLocs.size()) { plgText(ITPARENT, "IterHierc", "Parent out of chunk index"); return 0; }
    const bsVec<cmRecord::Evt>& pchunkData = _record->getEventChunk(pscopeChunkLocs[mrIdx], &rt.levels[_nestingLevel-1].scopeLastLiveEvtChunk);
    if(eIdx>=pchunkData.size()) { plgText(ITPARENT, "IterHierc", "Parent out of chunk data"); return 0; }
    const cmRecord::Evt& pevt = pchunkData[eIdx];
    plgAssert(ITPARENT, pevt.flags&PL_FLAG_SCOPE_BEGIN);

    // Get the duration of the parent scope
    if(++eIdx==cmChunkSize) ++mrIdx; // Next scope of a "begin" is the following one
    if(mrIdx>=pscopeChunkLocs.size()) { plgText(ITPARENT, "IterHierc", "End of record (1)"); return 0; } // No end scope: current is not valid
    const bsVec<cmRecord::Evt>& pchunkData2 = _record->getEventChunk(pscopeChunkLocs[mrIdx], &rt.levels[_nestingLevel-1].scopeLastLiveEvtChunk);
    if(eIdx>=pchunkData2.size()) { plgText(ITPARENT, "IterHierc", "End of record (2)"); return 0; }  // No end scope: current is not valid
    return pchunkData2[eIdx].vS64 - pevt.vS64;
}


// List parents, starting from current element (idx 0) to top of tree (last idx)
void
cmRecordIteratorHierarchy::getParents(bsVec<Parent>& parents)
{
    plgScope(ITPARENT, "cmRecordIteratorHierarchy::getParents");
    const cmRecord::Thread& rt = _record->threads[_threadId];
    int nestingLevel = _nestingLevel;
    u32 lIdx         = _lIdx;
    parents.clear();

    // Get current item
    if(nestingLevel<0 || nestingLevel>=rt.levels.size())  { plgText(ITPARENT, "IterHierc", "Current out of nesting level"); return; }
    const bsVec<chunkLoc_t>& nonScopeChunkLocs = rt.levels[nestingLevel].nonScopeChunkLocs;
    const bsVec<chunkLoc_t>& scopeChunkLocs    = rt.levels[nestingLevel].scopeChunkLocs;
    const bsVec<chunkLoc_t>* chunkLocs = GET_ISFLAT(lIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = GET_ISFLAT(lIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
    int mrIdx = GET_LIDX(lIdx)/cmChunkSize;
    int eIdx  = GET_LIDX(lIdx)%cmChunkSize;
    if(mrIdx>=chunkLocs->size())  { plgText(ITPARENT, "IterHierc", "Current out of chunk index"); return; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk((*chunkLocs)[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size()) { plgText(ITPARENT, "IterHierc", "Current out of chunk data"); return; }
    cmRecord::Evt evt = chunkData[eIdx];

    while(nestingLevel>0) {
        plgScope(ITPARENT, "Loop on level");
        plgVar(ITPARENT, nestingLevel, GET_LIDX(lIdx), GET_ISFLAT(lIdx));

        // Get parent
        const bsVec<chunkLoc_t>& pscopeChunkLocs = rt.levels[nestingLevel-1].scopeChunkLocs;
        mrIdx = GET_LIDX(evt.parentLIdx)/cmChunkSize;
        eIdx  = GET_LIDX(evt.parentLIdx)%cmChunkSize;
        if(mrIdx>=pscopeChunkLocs.size()) { plgText(ITPARENT, "IterHierc", "Parent out of chunk index"); return; }
        const bsVec<cmRecord::Evt>& pchunkData = _record->getEventChunk(pscopeChunkLocs[mrIdx], &rt.levels[_nestingLevel-1].scopeLastLiveEvtChunk);
        if(eIdx>=pchunkData.size()) { plgText(ITPARENT, "IterHierc", "Parent out of chunk data"); return; }
        const cmRecord::Evt& pevt = pchunkData[eIdx];
        plgAssert(ITPARENT, pevt.flags&PL_FLAG_SCOPE_BEGIN);

        if((evt.flags&PL_FLAG_TYPE_MASK)<PL_FLAG_TYPE_MEMORY_FIRST || (evt.flags&PL_FLAG_TYPE_MASK)>PL_FLAG_TYPE_MEMORY_LAST) {
            parents.push_back( { evt, lIdx} );
        }
        else plgText(ITPARENT, "IterHierc", "Skip memory event");

        // Set parent as current
        --nestingLevel;
        lIdx = evt.parentLIdx;
        evt  = pevt;
    }

    // Store the top level (we ignore non-scope)
    plgAssert(ITPARENT, !GET_ISFLAT(lIdx)); // No non-scope item on top level, by design
    parents.push_back( { evt, lIdx } );
}


bool
cmRecordIteratorHierarchy::getItem(int& nestingLevel, u32& lIdx, cmRecord::Evt& evt, s64& scopeEndTimeNs, bool noMoveToNext)
{
    // Get location infos
    plgScope(ITTEXT, "getItem");

    // Do not go to the next one if the iterator has just been constructed
    if(!noMoveToNext && !_isJustInitialized) {
        // Loop to filters memory events
        while(1) {
            next_();
            if(!getItem(nestingLevel, lIdx, evt, scopeEndTimeNs, true)) return false;
            int eType = evt.flags&PL_FLAG_TYPE_MASK;
            if(eType!=PL_FLAG_TYPE_ALLOC && eType!=PL_FLAG_TYPE_DEALLOC) break;
        }
    }
    _isJustInitialized = false;

    const cmRecord::Thread& rt = _record->threads[_threadId];
    if(rt.levels.empty()) return false;
    const bsVec<chunkLoc_t>& nonScopeChunkLocs = rt.levels[_nestingLevel].nonScopeChunkLocs;
    const bsVec<chunkLoc_t>& scopeChunkLocs    = rt.levels[_nestingLevel].scopeChunkLocs;
    const bsVec<chunkLoc_t>* chunkLocs = GET_ISFLAT(_lIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = GET_ISFLAT(_lIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
    int mrIdx = GET_LIDX(_lIdx)/cmChunkSize;
    int eIdx  = GET_LIDX(_lIdx)%cmChunkSize;
    if(mrIdx>=chunkLocs->size()) { plgText(ITTEXT, "IterHierc", "End of record (1)"); return false; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk((*chunkLocs)[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size()) { plgText(ITTEXT, "IterHierc", "End of record (2)"); return false; }

    // Fill output
    lIdx           = _lIdx;
    nestingLevel   = _nestingLevel;
    evt            = chunkData[eIdx];
    scopeEndTimeNs = -1; // Filled later below, for 'scope start'

    // Logging
    plgData(ITTEXT, "Name of returned item", ((evt.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_ALLOC || (evt.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_DEALLOC)?
                 "[Memory]" : _record->getString(evt.nameIdx).value.toChar());
    plgVar(ITTEXT, evt.flags, _nestingLevel, GET_LIDX(_lIdx), GET_ISFLAT(_lIdx));

    // Get the duration of the scope if 'start' (end scope is the next one), which is part of the output
    if(evt.flags&PL_FLAG_SCOPE_BEGIN) {
        if(++eIdx==cmChunkSize) ++mrIdx; // Next scope of a "begin" is the following one
        if(mrIdx>=scopeChunkLocs.size()) { plgText(ITTEXT, "IterHierc", "End of record (3)"); return false; } // No end scope: current is not valid
        const bsVec<cmRecord::Evt>& chunkData2 = _record->getEventChunk(scopeChunkLocs[mrIdx], &rt.levels[_nestingLevel].scopeLastLiveEvtChunk);
        if(eIdx>=chunkData2.size()) { plgText(ITTEXT, "IterHierc", "End of record (4)"); return false; }  // No end scope: current is not valid
        scopeEndTimeNs = chunkData2[eIdx].vS64;
    }

    return true; // Success
}


void
cmRecordIteratorHierarchy::next_(void)
{
    // Get the current event
    plgScope(ITTEXT, "next");
    const cmRecord::Thread& rt = _record->threads[_threadId];
    const bsVec<chunkLoc_t>& nonScopeChunkLocs = rt.levels[_nestingLevel].nonScopeChunkLocs;
    const bsVec<chunkLoc_t>& scopeChunkLocs    = rt.levels[_nestingLevel].scopeChunkLocs;
    const bsVec<chunkLoc_t>* chunkLocs = GET_ISFLAT(_lIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = GET_ISFLAT(_lIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
    int mrIdx = GET_LIDX(_lIdx)/cmChunkSize;
    int eIdx  = GET_LIDX(_lIdx)%cmChunkSize;
    if(mrIdx>=chunkLocs->size()) { plgText(ITTEXT, "IterHierc", "End of record (1)"); return; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk((*chunkLocs)[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size()) { plgText(ITTEXT, "IterHierc", "End of record (2)"); return; }
    const cmRecord::Evt& evt = chunkData[eIdx];

    // Logging
    plgVar(ITTEXT, _nestingLevel, GET_LIDX(_lIdx), GET_ISFLAT(_lIdx), rt.levels.size(), chunkLocs->size());

    // Case begin of scope
    if(evt.flags&PL_FLAG_SCOPE_BEGIN) {
        plgScope(ITTEXT, "Item is begin of scope");
        // Downwards to the first child
        if(_nestingLevel+1<rt.levels.size()) {
            plgScope(ITTEXT, "Search for first child");
            const bsVec<chunkLoc_t>& cnonScopeChunkLocs = rt.levels[_nestingLevel+1].nonScopeChunkLocs;
            const bsVec<chunkLoc_t>& cscopeChunkLocs    = rt.levels[_nestingLevel+1].scopeChunkLocs;
            const bsVec<chunkLoc_t>* cchunkLocs         = GET_ISFLAT(evt.linkLIdx)? &cnonScopeChunkLocs : &cscopeChunkLocs;
            const bsVec<cmRecord::Evt>* clastLiveEvtChunk = GET_ISFLAT(evt.linkLIdx)? &rt.levels[_nestingLevel+1].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel+1].scopeLastLiveEvtChunk;
            int cmrIdx = GET_LIDX(evt.linkLIdx)/cmChunkSize;
            int ceIdx  = GET_LIDX(evt.linkLIdx)%cmChunkSize;
            plgData(ITTEXT, "Child lidx", GET_LIDX(evt.linkLIdx));
            plgData(ITTEXT, "Child type", GET_ISFLAT(evt.linkLIdx));

            if(cmrIdx<cchunkLocs->size()) {
                const bsVec<cmRecord::Evt>& cchunkData = _record->getEventChunk((*cchunkLocs)[cmrIdx], clastLiveEvtChunk);
                if(ceIdx<cchunkData.size() && cchunkData[ceIdx].parentLIdx==(u32)_lIdx) {
                    // Update the level and level index
                    plgText(ITTEXT, "IterHierc", "Child matches");
                    ++_nestingLevel;
                    _lIdx = evt.linkLIdx;
                    return;
                }
            }
        }

        // No or skipped children: next scope is the following one at same level
        if(++eIdx==cmChunkSize) ++mrIdx;
        if(mrIdx>=scopeChunkLocs.size()) { plgText(ITTEXT, "IterHierc", "End of record (3)"); return; } // No end scope: current is not valid
        const bsVec<cmRecord::Evt>& chunkData2 = _record->getEventChunk(scopeChunkLocs[mrIdx], &rt.levels[_nestingLevel].scopeLastLiveEvtChunk);
        if(eIdx>=chunkData2.size()) { plgText(ITTEXT, "IterHierc", "End of record (4)"); return; }  // No end scope: current is not valid
        // Update the level index
        _lIdx = mrIdx*cmChunkSize+eIdx;
        return;
    }

    // If the next item has the same parent, it is our 'next'
    const bsVec<chunkLoc_t>*    nchunkLocs        = GET_ISFLAT(evt.linkLIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
    const bsVec<cmRecord::Evt>* nlastLiveEvtChunk = GET_ISFLAT(evt.linkLIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;

    int nmrIdx = GET_LIDX(evt.linkLIdx)/cmChunkSize;
    int neIdx  = GET_LIDX(evt.linkLIdx)%cmChunkSize;
    if(nmrIdx<nchunkLocs->size()) {
        const bsVec<cmRecord::Evt>& nchunkData = _record->getEventChunk((*nchunkLocs)[nmrIdx], nlastLiveEvtChunk);
        if(neIdx<nchunkData.size() && nchunkData[neIdx].parentLIdx==evt.parentLIdx) {
            plgText(ITTEXT, "IterHierc", "Same parent");
            // Update the level index
           _lIdx = evt.linkLIdx;
            return;
        }
    }

    // Ensure that we can go upward
    if(_nestingLevel==0) {
        plgText(ITTEXT, "IterHierc", "Next is end of record at top level)");
        _lIdx = evt.linkLIdx;
        return;
    }
    plgAssert(ITTEXT, evt.parentLIdx!=PL_INVALID); // Level zero is processed by previous test

    // If the next item has a different parent, go upwards to the parent's end scope
    plgText(ITTEXT, "IterHierc", "Different parent");
    --_nestingLevel;
    _lIdx = evt.parentLIdx+1;
}


bool
cmRecordIteratorHierarchy::rewind(void)
{
    // Filters memory events
    int nestingLevel;
    u32 lIdx;
    s64 scopeEndTimeNs;
    cmRecord::Evt evt;
    while(1) {
        rewind_();
        if(!getItem(nestingLevel, lIdx, evt, scopeEndTimeNs, true)) return false;
        int eType = evt.flags&PL_FLAG_TYPE_MASK;
        if(eType!=PL_FLAG_TYPE_ALLOC && eType!=PL_FLAG_TYPE_DEALLOC) break;
    }
    return true;
}


void
cmRecordIteratorHierarchy::rewind_(void)
{
    plgScope(ITREWIND, "cmRecordIteratorHierarchy::rewind_");
    plgVar(ITREWIND, _nestingLevel, GET_LIDX(_lIdx), GET_ISFLAT(_lIdx));

    // Get current item
    if(_nestingLevel==0 && _lIdx==0) { plgText(ITREWIND, "IterHierc", "Top index already"); return; }
    const cmRecord::Thread& rt = _record->threads[_threadId];
    const bsVec<chunkLoc_t>& nonScopeChunkLocs = rt.levels[_nestingLevel].nonScopeChunkLocs;
    const bsVec<chunkLoc_t>& scopeChunkLocs    = rt.levels[_nestingLevel].scopeChunkLocs;
    const bsVec<chunkLoc_t>* chunkLocs = GET_ISFLAT(_lIdx)? &nonScopeChunkLocs : &scopeChunkLocs;
    const bsVec<cmRecord::Evt>* lastLiveEvtChunk = GET_ISFLAT(_lIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
    int mrIdx = GET_LIDX(_lIdx)/cmChunkSize;
    int eIdx  = GET_LIDX(_lIdx)%cmChunkSize;
    if(mrIdx>=chunkLocs->size())  { plgText(ITREWIND, "IterHierc", "Item out of chunk index"); return; }
    const bsVec<cmRecord::Evt>& chunkData = _record->getEventChunk((*chunkLocs)[mrIdx], lastLiveEvtChunk);
    if(eIdx>=chunkData.size())  { plgText(ITREWIND, "IterHierc", "Item out of chunk data"); return; }
    const cmRecord::Evt& evt = chunkData[eIdx];

    // Heuristic 1: try the previous event
    if(eIdx>0 || mrIdx>0) {
        const cmRecord::Evt& evtPrev = (eIdx>0)? chunkData[eIdx-1] : _record->getEventChunk((*chunkLocs)[mrIdx-1], lastLiveEvtChunk)[cmChunkSize-1];
        if((evtPrev.linkLIdx==_lIdx && evtPrev.parentLIdx==evt.parentLIdx) ||  // Previous event is of the same kind, with same parent, and points toward current event
           ((evt.flags&PL_FLAG_SCOPE_END) && evtPrev.linkLIdx==PL_INVALID)) {    // Previous event is a begin block with same parent and without children (implies current is end block...)
            _lIdx -= 1;
            return;
        }
    }

    // Heuristic 2: Check if the parent points on current event
    if(_nestingLevel>0 && evt.parentLIdx!=PL_INVALID) {
        const bsVec<chunkLoc_t>& pChunkLocs = rt.levels[_nestingLevel-1].scopeChunkLocs;
        const bsVec<cmRecord::Evt>* pLastLiveEvtChunk =  &rt.levels[_nestingLevel-1].scopeLastLiveEvtChunk;
        int pmrIdx = GET_LIDX(evt.parentLIdx)/cmChunkSize;
        int peIdx  = GET_LIDX(evt.parentLIdx)%cmChunkSize;
        const cmRecord::Evt& evtParent = _record->getEventChunk(pChunkLocs[pmrIdx], pLastLiveEvtChunk)[peIdx]; // Should exist by construction
        if(evtParent.linkLIdx==_lIdx) {
            _lIdx = evt.parentLIdx;
            _nestingLevel -= 1;
            return;
        }
    }
    // From here, the previous event is of the "other" kind (flat or not flat)

    // Initialize the reverse tracing with the parent (only forward information is available)
    struct TraceItem { int nestingLevel; u32 lIdx; };
    TraceItem last, current;
    bool goToChild = true;
    u32  stopIfParentDiffers = PL_INVALID;
    if(evt.flags&PL_FLAG_SCOPE_END) {
        // For "end", we target the last child (through "begin" at same level). If no child, then the previous "begin"
        plgData(ITREWIND, "Push begin level", _nestingLevel);
        plgData(ITREWIND, "Push begin lIdx", _lIdx-1);
        stopIfParentDiffers = _lIdx-1;
        last = current = { _nestingLevel, _lIdx-1 };
        plgAssert(ITTEXT, eIdx>=0);
        goToChild = (_nestingLevel<rt.levels.size()-1); // Do not go to child if no level below...
    }
    else if(_nestingLevel>0) {
        // For "begin" and non-scope, we iterate over children (through parent) until we find initial one
        plgData(ITREWIND, "Push parent level", _nestingLevel-1);
        plgData(ITREWIND, "Push parent lIdx", evt.parentLIdx);
        last = current = { _nestingLevel-1, evt.parentLIdx };
    }
    else {
        // If we are already at top level, we start at origin
        plgText(ITREWIND, "IterHierc", "Push origin");
        goToChild = false;
        last = current = {0, 0};
    }

    // Trace until we find back the current item
    while(1) {
        plgScope(ITREWIND, "Loop");
        plgVar(ITREWIND, current.nestingLevel, GET_LIDX(current.lIdx), GET_ISFLAT(current.lIdx));
        if(current.nestingLevel==_nestingLevel && current.lIdx==_lIdx) {
            plgText(ITREWIND, "IterHierc", "Initial position found!");
            break;
        }

        // Get current item
        const bsVec<chunkLoc_t>& nonScopeChunkLocs2   = rt.levels[current.nestingLevel].nonScopeChunkLocs;
        const bsVec<chunkLoc_t>& scopeChunkLocs2      = rt.levels[current.nestingLevel].scopeChunkLocs;
        const bsVec<chunkLoc_t>* chunkLocs2           = GET_ISFLAT(current.lIdx)? &nonScopeChunkLocs2 : &scopeChunkLocs2;
        const bsVec<cmRecord::Evt>* lastLiveEvtChunk2 = GET_ISFLAT(current.lIdx)? &rt.levels[_nestingLevel].nonScopeLastLiveEvtChunk : &rt.levels[_nestingLevel].scopeLastLiveEvtChunk;
        mrIdx = GET_LIDX(current.lIdx)/cmChunkSize;
        eIdx  = GET_LIDX(current.lIdx)%cmChunkSize;
        if(mrIdx>=chunkLocs2->size())  { plgText(ITREWIND, "IterHierc", "Current out of chunk index"); break; }
        const bsVec<cmRecord::Evt>& chunkData2 = _record->getEventChunk((*chunkLocs2)[mrIdx], lastLiveEvtChunk2);
        if(eIdx>=chunkData2.size())  { plgText(ITREWIND, "IterHierc", "Current out of chunk data"); break; }
        const cmRecord::Evt& evt2 = chunkData2[eIdx];

        if(!goToChild && stopIfParentDiffers!=PL_INVALID && evt2.parentLIdx!=stopIfParentDiffers) {
            plgText(ITREWIND, "IterHierc", "Parent differs!");
            break;
        }

        last = current;
        if(goToChild) { // Go to child if asked (once)
            plgText(ITREWIND, "IterHierc", "go to child");
            goToChild = false;
            plgAssert(ITREWIND, evt2.flags&PL_FLAG_SCOPE_BEGIN);
            current = {last.nestingLevel+1, evt2.linkLIdx };
        }
        else { // Go to next item at the same level
            plgText(ITREWIND, "IterHierc", "go to next");
            current = {last.nestingLevel, (evt2.flags&PL_FLAG_SCOPE_BEGIN)? (last.lIdx+1) : evt2.linkLIdx};
        }
    }

    // Select the previous item
    plgScope(ITREWIND, "Conclusion");
    _nestingLevel = last.nestingLevel;
    _lIdx         = last.lIdx;
    plgVar(ITREWIND, _nestingLevel, GET_LIDX(_lIdx), GET_ISFLAT(_lIdx));
}



// ===============================
// Functions
// ===============================

u64
cmGetParentDurationNs(const cmRecord* record, int threadId, int nestingLevel, u32 lIdx)
{
    cmRecordIteratorHierarchy it(record, threadId, nestingLevel, lIdx);
    return it.getParentDurationNs();
}



// Used by the text views
void
cmGetRecordPosition(const cmRecord* record, int threadId, s64 targetTimeNs,
                     int& outNestingLevel, u32& outLIdx)
{
    plgScope(ITSCROLL, "cmGetRecordPosition");
    plgVar(ITSCROLL, threadId, recordRatio);
    plgData(ITSCROLL, "Target date (ns)", targetTimeNs);

    const cmRecord::Thread& rt = record->threads[threadId];
    s64 bestTimeNs   = 0;
    outNestingLevel  = 0;
    outLIdx          = 0;

    int nestingLevel = 0;
    while(nestingLevel<rt.levels.size()) {
        const bsVec<chunkLoc_t>& chunkLocs = rt.levels[nestingLevel].scopeChunkLocs;
        if(chunkLocs.empty()) break;
        u32 startLIdx = 0;
        u32 endLIdx   = (chunkLocs.size()-1)*cmChunkSize + record->getEventChunk(chunkLocs.back(),
                                                                                 &rt.levels[nestingLevel].scopeLastLiveEvtChunk).size() - 1; // Exact last LIdx (last chunk may be partial)

        bool isInsideAScope = false;
        bool isNewLevel     = true;

        while(1) {
            // Get the lIdx to test
            u32 middleLIdx = (startLIdx+(endLIdx-startLIdx)/2)&(~1); // "Begin" are on even indexes
            if(!isNewLevel && middleLIdx==startLIdx) break;
            isNewLevel = false; // Even if middle==start, we can go deeper in the nesting, so we shall try at least once

            // Get the middle event date
            int mrIdx = middleLIdx/cmChunkSize;
            int eIdx  = middleLIdx%cmChunkSize;
            if(mrIdx>=chunkLocs.size()) break;
            const bsVec<cmRecord::Evt>& chunkData = record->getEventChunk(chunkLocs[mrIdx], &rt.levels[nestingLevel].scopeLastLiveEvtChunk);
            if(eIdx>=chunkData.size()) break;
            const cmRecord::Evt& middleEvt = chunkData[eIdx];
            plAssert(middleEvt.flags&PL_FLAG_SCOPE_BEGIN); // Because even index

            // Update the best position so far
            if(bsAbs(bestTimeNs-targetTimeNs)>bsAbs(middleEvt.vS64-targetTimeNs)) {
                outNestingLevel = nestingLevel;
                outLIdx         = middleLIdx;
                bestTimeNs      = middleEvt.vS64;
            }
            if(bsAbs(bestTimeNs-targetTimeNs)>bsAbs(chunkData[eIdx+1].vS64-targetTimeNs)) {
                outNestingLevel = nestingLevel;
                outLIdx         = middleLIdx+1;
                bestTimeNs      = chunkData[eIdx+1].vS64;
            }

            // Scope start date after target date
            if(middleEvt.vS64>targetTimeNs) { endLIdx = middleLIdx; continue; }

            // Check if the target date is inside the scope
            if(chunkData[eIdx+1].vS64>=targetTimeNs) { isInsideAScope = true; break; }

            // Scope end before target date
            startLIdx = middleLIdx;
        } // End of dichotomy to find the scope which includes the target date

        if(!isInsideAScope || nestingLevel+1>=rt.levels.size()) break;
        ++nestingLevel;
    }

    // Set start infos
    plgData(ITSCROLL, "Final level", outNestingLevel);
    plgData(ITSCROLL, "Final lIdx", outLIdx);
}
