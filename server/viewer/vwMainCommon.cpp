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

// This file implements some helpers for the viewer and some common GUI parts

// System
#include <algorithm>
#include <cmath>
#include <cinttypes>

// Internal
#include "bsKeycode.h"
#include "cmRecord.h"
#include "cmPrintf.h"
#include "vwConst.h"
#include "vwMain.h"
#include "vwConfig.h"



// Display helpers
// ===============

int
vwMain::getId(void)
{
    if(_idPool.empty()) _idPool.push_back(_idMax++);
    int id = _idPool.back();
    _idPool.pop_back();
    return id;
}


void
vwMain::releaseId(int id)
{
#if 0
    for(int id2 : _idPool) plAssert(id2!=id);
#endif
    _idPool.push_back(id);
}


// Nice formatters are not thread-safe, but ok as we display only in one thread
const char*
vwMain::getNiceDate(const bsDate& date, const bsDate& now) const
{
    static char outBuf[32];
    static const char* months[13] = { "NULL", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if(date.year==now.year && date.month==now.month && date.day==now.day) {
        snprintf(outBuf, sizeof(outBuf), "Today %02d:%02d:%02d", date.hour, date.minute, date.second);
    } else {
        snprintf(outBuf, sizeof(outBuf), "%s %02d %02d:%02d:%02d", months[(date.month>0 && date.month<=12)?date.month:0], date.day, date.hour, date.minute, date.second);
    }
    return outBuf;
}


const char*
vwMain::getNiceTime(s64 ns, s64 tickNs, int bank) const
{
    static char outBuf1[64];
    static char outBuf2[64];
    char* outBuf = (bank==0)? outBuf1 : outBuf2;
    int   offset = snprintf(outBuf, sizeof(outBuf1), "%llds", ns/1000000000LL);
    if(tickNs<60000000000LL) offset += snprintf(outBuf+offset, sizeof(outBuf1)-offset, ":%03lldms", (ns/1000000LL)%1000); // Below 1 mn, always display at least ms
    if(tickNs<1000000LL)     offset += snprintf(outBuf+offset, sizeof(outBuf1)-offset, ":%03lldµs", (ns/1000LL)%1000);
    if(tickNs<1000LL)                  snprintf(outBuf+offset, sizeof(outBuf1)-offset, ":%03" PRId64 "ns", (ns%1000));
    return outBuf;
}


const char*
vwMain::getNiceFullTime(s64 ns) const
{
    static char outBuf[64];
    snprintf(outBuf, sizeof(outBuf), "%lldh:%02lldmn:%02llds.%09lld",
             (ns/3600000000000LL),
             (ns/60000000000LL)%60,
             (ns/1000000000LL)%60,
             (ns%1000000000LL));
    return outBuf;
}


int
vwMain::getFormattedTimeStringCharQty(int timeFormat)
{
    constexpr int timeStrCharQtyArray[3] = { 16+2, 23+2, 22+2 }; // 2 char of margin
    plAssert(timeFormat>=0 && timeFormat<3, timeFormat);
    return timeStrCharQtyArray[timeFormat];
}


const char*
vwMain::getFormattedTimeString(s64 ns, int timeFormat) const
{
    static char outBuf[64];
    if     (timeFormat==1) return getNiceTime(ns, 0);
    else if(timeFormat==2) return getNiceFullTime(ns);
    snprintf(outBuf, sizeof(outBuf), "%.9fs", 0.000000001*(double)ns);
    return outBuf;
}


const char*
vwMain::getNiceDuration(s64 ns, s64 displayRangeNs, int bank) const
{
    static char outBuf1[32];
    static char outBuf2[32];
    char* outBuf = (bank==0)? outBuf1 : outBuf2;
    if(displayRangeNs<=0) displayRangeNs = ns;
    if     (displayRangeNs<1000      ) snprintf(outBuf, sizeof(outBuf1), "%" PRId64 " ns", ns);
    else if(displayRangeNs<1000000   ) snprintf(outBuf, sizeof(outBuf1), "%.2f µs", 0.001*ns);
    else if(displayRangeNs<1000000000) snprintf(outBuf, sizeof(outBuf1), "%.2f ms", 0.000001*ns);
    else                               snprintf(outBuf, sizeof(outBuf1), "%.2f s",  0.000000001*ns);
    return outBuf;
}


const char*
vwMain::getNiceByteSize(s64 byteSize) const
{
    static char outBuf[32];
    if     (byteSize<1000      ) snprintf(outBuf, sizeof(outBuf), "%" PRId64 " B", byteSize);
    else if(byteSize<1000000   ) snprintf(outBuf, sizeof(outBuf), "%.2f KB", 0.001*byteSize);
    else if(byteSize<1000000000) snprintf(outBuf, sizeof(outBuf), "%.2f MB", 0.000001*byteSize);
    else                         snprintf(outBuf, sizeof(outBuf), "%.2f GB", 0.000000001*byteSize);
    return outBuf;
}


const char*
vwMain::getNiceBigPositiveNumber(u64 number, int bank) const
{
    static char outBuf1[32];
    static char outBuf2[32];
    char* outBuf = (bank==0)? outBuf1 : outBuf2;
    u64 divider = 1000000000000000000LL;
    while(divider>1 && (number/divider)==0) divider /= 1000;
    bool displayStarted = false;
    int offset = 0;
    while(1) {
        int d = (int)(number/divider);
        offset += snprintf(outBuf+offset, sizeof(outBuf1)-offset, displayStarted?" %03d":"%d", d);
        if(divider==1) break;
        number -= d*divider;
        divider /= 1000;
        displayStarted = true;
    }
    return outBuf;
}


// Get the value as string
const char*
vwMain::getValueAsChar(int flags, double value, double displayRange, bool isHexa, int bank, bool withUnit) const
{
    static char valueStr1[128];
    static char valueStr2[128];
    char* valueStr = (bank==0)? valueStr1 : valueStr2;

    // Case scope or lock use
    if((flags&PL_FLAG_SCOPE_BEGIN) || (flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_LOCK_ACQUIRED) {
        if(withUnit) return getNiceDuration((s64)value, (s64)displayRange, bank);
        snprintf(valueStr, sizeof(valueStr1), "%" PRId64, (s64)value);
        return valueStr;
    }
    // Case string
    else if((flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_DATA_STRING) return _record->getString((int)bsRound(value)).value.toChar();
    // Case value
    else {
#define CONVERT_VALUE_INT(formatStr, castStr)   snprintf(valueStr, sizeof(valueStr1), formatStr, (castStr)bsRound(value)); return valueStr;
#define CONVERT_VALUE_FLOAT(formatStr, castStr) snprintf(valueStr, sizeof(valueStr1), formatStr, (castStr)value); return valueStr;
        switch(flags&PL_FLAG_TYPE_MASK) {
        case PL_FLAG_TYPE_DATA_S32:    CONVERT_VALUE_INT(isHexa?"%X":"%d",  int); break;
        case PL_FLAG_TYPE_DATA_U32:    CONVERT_VALUE_INT(isHexa?"%X":"%u",  u32); break;
        case PL_FLAG_TYPE_DATA_S64:    CONVERT_VALUE_INT(isHexa?"%lX":"%ld", s64); break;
        case PL_FLAG_TYPE_DATA_U64:    CONVERT_VALUE_INT(isHexa?"%lX":"%lu", u64); break;
        case PL_FLAG_TYPE_DATA_FLOAT:  CONVERT_VALUE_FLOAT("%f",   float); break;
        case PL_FLAG_TYPE_DATA_DOUBLE: CONVERT_VALUE_FLOAT("%lf", double); break;
        case PL_FLAG_TYPE_LOCK_NOTIFIED: return _record->getString(_record->threads[(int)value].nameIdx).value.toChar(); break;
        default: plAssert(0, "bug...", flags);
        }
    }
    return 0;
};


const char*
vwMain::getValueAsChar(const cmRecord::Evt& e) const
{
    static char valueStr[128];
    int  flags  = e.flags;
    bool isHexa = _record->getString(e.nameIdx).isHexa;

    if(flags&PL_FLAG_SCOPE_BEGIN) {
        return getNiceDuration(e.vS64);
    } else {
        switch(flags&PL_FLAG_TYPE_MASK) {
        case PL_FLAG_TYPE_DATA_S32:    snprintf(valueStr, sizeof(valueStr), isHexa?"%X":"%d",   e.vInt); break;
        case PL_FLAG_TYPE_DATA_U32:    snprintf(valueStr, sizeof(valueStr), isHexa?"%X":"%u",   e.vU32); break;
        case PL_FLAG_TYPE_DATA_S64:    snprintf(valueStr, sizeof(valueStr), isHexa?"%lX":"%ld", e.vS64); break;
        case PL_FLAG_TYPE_DATA_U64:    snprintf(valueStr, sizeof(valueStr), isHexa?"%lX":"%lu", e.vU64); break;
        case PL_FLAG_TYPE_DATA_FLOAT:  snprintf(valueStr, sizeof(valueStr), "%f",   e.vFloat);    break;
        case PL_FLAG_TYPE_DATA_DOUBLE: snprintf(valueStr, sizeof(valueStr), "%lf",  e.vDouble);   break;
        case PL_FLAG_TYPE_DATA_STRING:   return _record->getString(e.vStringIdx ).value.toChar(); break;
        case PL_FLAG_TYPE_LOCK_NOTIFIED: return _record->getString(_record->threads[e.threadId].nameIdx).value.toChar(); break;
        default: plAssert(0, "bug...", flags); break;
        }
    }
    return valueStr;
}


const char*
vwMain::getUnitFromFlags(int flags) const
{
    int eType = (flags&PL_FLAG_TYPE_MASK);
    if     (flags&PL_FLAG_SCOPE_BEGIN)         return "Duration";
    else if(eType==PL_FLAG_TYPE_DATA_STRING)   return "<Enum>";
    else if(eType==PL_FLAG_TYPE_LOG)           return "<Log>";
    else if(eType==PL_FLAG_TYPE_LOCK_ACQUIRED) return "<Lock>";
    else if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) return "<Lock notified>";
    return "";
}


const char*
vwMain::getElemName(const bsString& baseName, int flags)
{
    static char valueStr[128];
    switch(flags&PL_FLAG_TYPE_MASK) {
    case PL_FLAG_TYPE_LOCK_WAIT:     snprintf(valueStr, sizeof(valueStr), "<lock wait> %s",     baseName.toChar()); break;
    case PL_FLAG_TYPE_LOCK_ACQUIRED: snprintf(valueStr, sizeof(valueStr), "<lock acquired> %s", baseName.toChar()); break;
    case PL_FLAG_TYPE_LOCK_RELEASED: snprintf(valueStr, sizeof(valueStr), "<lock released> %s", baseName.toChar()); break;
    case PL_FLAG_TYPE_LOCK_NOTIFIED: snprintf(valueStr, sizeof(valueStr), "<lock notified> %s", baseName.toChar()); break;
    case PL_FLAG_TYPE_LOG:           snprintf(valueStr, sizeof(valueStr), "<log> %s",           baseName.toChar()); break;
    default:                         snprintf(valueStr, sizeof(valueStr), "%s",                 baseName.toChar());
    }
    return valueStr;
}



// Global record precomputations
// =============================

void
vwMain::precomputeRecordDisplay(void)
{
    // Is cache up to date?
    if(!_record || (!_liveRecordUpdated && ImGui::GetFontSize()==_lastFontSize)) return;

    // Update the full thread name list
    if(_liveRecordUpdated) {
        _fullThreadNames.clear();
        _fullThreadNames.reserve(_record->threads.size());
        for(const auto& t : _record->threads) {
            if(t.groupNameIdx>=0) _fullThreadNames.push_back(_record->getString(t.groupNameIdx).value + "/" + _record->getString(t.nameIdx).value);
            else                  _fullThreadNames.push_back(_record->getString(t.nameIdx).value);
        }
    }

    // Update the timeline header width
    _timelineHeaderWidth = 100.f; // Minimum value
    _timelineHeaderWidth = bsMax(_timelineHeaderWidth, ImGui::CalcTextSize("Locks & Resources").x);
    for(const auto& t : _record->threads) {
        if(t.groupNameIdx>=0) {
            _timelineHeaderWidth = bsMax(_timelineHeaderWidth, ImGui::CalcTextSize(_record->getString(t.groupNameIdx).value.toChar()).x);
        }
        _timelineHeaderWidth = bsMax(_timelineHeaderWidth, ImGui::CalcTextSize(_record->getString(t.nameIdx).value.toChar()).x);
    }
    _timelineHeaderWidth += 2.f*ImGui::GetTextLineHeightWithSpacing(); // For the triangle and a margin

    // Animate the live record visible time range
    if(_liveRecordUpdated) {
        constexpr s64 fixedRecordLengthBeforeMoveNs = 5000000000;
        const s64 recordDurationNs = _record->durationNs;
#define LOOP_LIVE_RANGE(array)                                          \
        for(auto& t : (array)) {                                        \
            if(recordDurationNs<=fixedRecordLengthBeforeMoveNs) {       \
                t.setView(0, recordDurationNs, true);                   \
                t.checkTimeBounds(recordDurationNs);                    \
            }                                                           \
            else if(t.isTouchingEnd) {                                  \
                s64 r = (t.timeRangeNs<=1)? fixedRecordLengthBeforeMoveNs : t.timeRangeNs; \
                t.setView(recordDurationNs-r, r, true);                 \
                t.checkTimeBounds(recordDurationNs);                    \
            }                                                           \
        }

        LOOP_LIVE_RANGE(_timelines);
        LOOP_LIVE_RANGE(_memTimelines);
        LOOP_LIVE_RANGE(_plots);
    }

    // Up to date
    _lastFontSize      = ImGui::GetFontSize();
}


// Time range based common methods
// ===============================

void
vwMain::TimeRangeBase::setView(s64 newStartTimeNs, s64 newTimeRangeNs, bool noTransition)
{
    if(newTimeRangeNs<1000) newTimeRangeNs = 1000; // 1 micro second minimum range
    if(animTimeUs>0 && animStartTimeNs2==newStartTimeNs && animTimeRangeNs2==newTimeRangeNs) return; // Already set
    animStartTimeNs1 = startTimeNs; animStartTimeNs2 = newStartTimeNs;
    animTimeRangeNs1 = timeRangeNs; animTimeRangeNs2 = newTimeRangeNs;
    bsUs_t currentTime = bsGetClockUs();
    animTimeUs = (animTimeUs==0)? currentTime: currentTime-bsMin((bsUs_t)(0.5f*vwConst::ANIM_DURATION_US), currentTime-animTimeUs);
    if(noTransition) animTimeUs -= vwConst::ANIM_DURATION_US; // So the animation time is already over
    isCacheDirty  = true;
}


void
vwMain::TimeRangeBase::ensureThreadVisibility(int threadId)
{
    viewThreadId = threadId;
}


void
vwMain::TimeRangeBase::checkTimeBounds(s64 recordDurationNs)
{
    if(startTimeNs<0.)                           { startTimeNs = 0; isCacheDirty = true; }
    if(startTimeNs+timeRangeNs>recordDurationNs) { startTimeNs = recordDurationNs-timeRangeNs; isCacheDirty = true; }
    if(startTimeNs<0.)                           { startTimeNs = 0; timeRangeNs = recordDurationNs; isCacheDirty = true; }
    isTouchingEnd = (startTimeNs+timeRangeNs>=recordDurationNs);
}


void
vwMain::TimeRangeBase::updateAnimation(void)
{
    if(animTimeUs<=0) return;
    bsUs_t currentTimeUs = bsGetClockUs();
    double ratio = sqrt(bsMin((double)(currentTimeUs-animTimeUs)/vwConst::ANIM_DURATION_US, 1.)); // Sqrt for more reactive start
    startTimeNs = (s64)(ratio*animStartTimeNs2+(1.-ratio)*animStartTimeNs1);
    timeRangeNs = (s64)(ratio*animTimeRangeNs2+(1.-ratio)*animTimeRangeNs1);
    if(ratio==1.) animTimeUs = 0;
    isCacheDirty = true;
}


// Iterator aggregator
// ===================

void
vwMain::AggregatedIterator::init(cmRecord* initRecord, s64 initStartTimeNs, double nsPerPix,
                                 const bsVec<int>& logElemIdxArray, const bsVec<int>& hTreeElemIdxArray)
{
    // Loop on elems
    logElemIts.clear();
    logElemsEvts.clear();
    hTreeElemIts.clear();
    hTreeElemsEvts.clear();
    record      = initRecord;
    startTimeNs = initStartTimeNs;

    cmRecord::Evt e;
    bsVec<cmLogParam> params;
    char tmpStr[512];
    u32  lIdx;
    int  lineQty;
    bool isValid, isCoarse;
    s64  timeNs;
    double value;

    // Log elems
    for(int elemIdx : logElemIdxArray) {
        // Store the iterator
        logElemIts.push_back(cmRecordIteratorLog(record, elemIdx, startTimeNs, nsPerPix));
        // And the first element after the date
        while((isValid=logElemIts.back().getNextLog(isCoarse, e, params)) && e.vS64<startTimeNs) ;
        if(!isValid) { e.vS64 = -1; tmpStr[0] = 0; lineQty = 1; }
        else {
            const cmRecord::String& s = record->getString(e.filenameIdx);
            cmVsnprintf(tmpStr, sizeof(tmpStr), s.value.toChar(), record, params);
            lineQty = s.lineQty;
            for(const cmLogParam& p : params) {
                if(p.paramType==PL_FLAG_TYPE_DATA_STRING) lineQty += record->getString(p.vStringIdx).lineQty-1;
            }
        }
        plAssert(lineQty>=1);
        logElemsEvts.push_back({e, elemIdx, 0, 0, 0., tmpStr, lineQty });
    }
    logElemStartIts = logElemIts; // So that we can go 'backward' without recomputing the start

    // H-tree elems
    for(int elemIdx : hTreeElemIdxArray) {
        hTreeElemIts.push_back(cmRecordIteratorElem(record, elemIdx, startTimeNs, nsPerPix));
        while((lIdx=hTreeElemIts.back().getNextPoint(timeNs, value, e))!=PL_INVALID && timeNs<startTimeNs) { }
        if(lIdx==PL_INVALID) { e.vS64 = -1; timeNs = -1; }
        hTreeElemsEvts.push_back({e, elemIdx, lIdx, timeNs, value, "", 1 });
    }
    hTreeElemStartIts = hTreeElemIts;
}


bool
vwMain::AggregatedIterator::getNextEvent(AggCacheItem& evt)
{
    // Get the earliest iterator
    int earliestIdx  = -1;
    s64 earliestDate = -1;
    int itKind = -1;
    for(int i=0; i<logElemsEvts.size(); ++i) {
        s64 d = logElemsEvts[i].evt.vS64;
        if(d>=0 && (earliestIdx==-1 || d<earliestDate)) {
            earliestIdx  = i;
            earliestDate = d;
            itKind       = 0;
        }
    }
    for(int i=0; i<hTreeElemsEvts.size(); ++i) {
        s64 d = hTreeElemsEvts[i].timeNs;
        if(d>=0 && (earliestIdx==-1 || d<earliestDate)) {
            earliestIdx  = i;
            earliestDate = d;
            itKind       = 1;
        }
    }
    if(itKind<0) return false;

    // Store the event and refill the used iterator
    cmRecord::Evt e;
    if(itKind==0) {
        evt = logElemsEvts[earliestIdx];
        bool isValid, isCoarse;
        int lineQty;
        char tmpStr[512];
        bsVec<cmLogParam> params;
        isValid = logElemIts[earliestIdx].getNextLog(isCoarse, e, params);
        if(!isValid) { e.vS64 = -1; tmpStr[0] = 0; lineQty = 1; }
        else {
            const cmRecord::String& s = record->getString(e.filenameIdx);
            cmVsnprintf(tmpStr, sizeof(tmpStr), s.value.toChar(), record, params);
            lineQty = s.lineQty;
            for(const cmLogParam& p : params) {
                if(p.paramType==PL_FLAG_TYPE_DATA_STRING) lineQty += record->getString(p.vStringIdx).lineQty-1;
            }
        }
        plAssert(lineQty>=1);
        logElemsEvts[earliestIdx] = {e, logElemsEvts[earliestIdx].elemIdx, 0, 0, 0., tmpStr, lineQty };
    }
    else {
        evt = hTreeElemsEvts[earliestIdx];
        double value;
        s64    timeNs;
        u32    lIdx = hTreeElemIts[earliestIdx].getNextPoint(timeNs, value, e);
        if(lIdx==PL_INVALID) { e.vS64 = -1; timeNs = -1; }
        hTreeElemsEvts[earliestIdx] = {e, hTreeElemsEvts[earliestIdx].elemIdx, lIdx, timeNs, value, "", 1 };
    }

    return true;
}


s64
vwMain::AggregatedIterator::getPreviousTime(int rewindItemQty)
{
    // Initialize the return in time by getting the time for each event just before the start date
    bsVec<int> logOffsets(logElemStartIts.size());
    for(int i=0; i< logElemStartIts.size(); ++i) {
        logOffsets[i] = -1; // One event before the start date (iterator was post incremented once, hence the -1)
        logElemsEvts[i].evt.vS64 = logElemStartIts[i].getTimeRelativeIdx(logOffsets[i]); // Result is -1 if none
        if(logElemsEvts[i].evt.vS64>=startTimeNs) { // This case should happen all the time, except when reaching the end of the recorded info
            logElemsEvts[i].evt.vS64 = logElemStartIts[i].getTimeRelativeIdx(--logOffsets[i]);
        }
    }
    bsVec<int> hTreeOffsets(hTreeElemStartIts.size());
    for(int i=0; i< hTreeElemStartIts.size(); ++i) {
        hTreeOffsets[i] = -1; // One event before the start date (iterator was post incremented once, hence the -1)
        hTreeElemsEvts[i].timeNs = hTreeElemStartIts[i].getTimeRelativeIdx(hTreeOffsets[i]); // Result is -1 if none
        if(hTreeElemsEvts[i].timeNs>=startTimeNs) { // This case should happen all the time, except when reaching the end of the recorded info
            hTreeElemsEvts[i].timeNs = hTreeElemStartIts[i].getTimeRelativeIdx(--hTreeOffsets[i]);
        }
    }

    s64 previousTimeNs = -1;
    while((rewindItemQty--)>0) {
        // Store the earliest time
        int latestIdx = -1;
        s64 latestDate = -1;
        int itKind = -1;
        for(int i=0; i<logElemsEvts.size(); ++i) {
            if(logElemsEvts[i].evt.vS64>=0 && (latestIdx==-1 || logElemsEvts[i].evt.vS64>latestDate)) {
                latestIdx  = i;
                latestDate = logElemsEvts[i].evt.vS64;
                itKind     = 0;
            }
        }
        for(int i=0; i<hTreeElemsEvts.size(); ++i) {
            if(hTreeElemsEvts[i].timeNs>=0 && (latestIdx==-1 || hTreeElemsEvts[i].timeNs>latestDate)) {
                latestIdx  = i;
                latestDate = hTreeElemsEvts[i].timeNs;
                itKind     = 1;
            }
        }
        if(latestIdx<0) return previousTimeNs;
        previousTimeNs = latestDate;

        // Refill the used iterator
        if(itKind==0) {
            logElemsEvts[latestIdx].evt.vS64 = logElemStartIts[latestIdx].getTimeRelativeIdx(--logOffsets[latestIdx]); // Result is -1 if none
        } else {
            hTreeElemsEvts[latestIdx].timeNs = hTreeElemStartIts[latestIdx].getTimeRelativeIdx(--hTreeOffsets[latestIdx]); // Result is -1 if none
        }
    }
    return previousTimeNs;
}


// Synchronisation helpers
// =======================

void
vwMain::allIsDirty(void)
{
#define LOOP_ALL_IS_DIRTY(array)				\
    for(auto& t : (array)) {                    \
        t.isCacheDirty = true;					\
	}
	LOOP_ALL_IS_DIRTY(_timelines);
    LOOP_ALL_IS_DIRTY(_memTimelines);
	LOOP_ALL_IS_DIRTY(_plots);
	LOOP_ALL_IS_DIRTY(_texts);
	LOOP_ALL_IS_DIRTY(_logViews);
}

void
vwMain::getSynchronizedRange(int syncMode, s64& startTimeNs, s64& timeRangeNs)
{
#define LOOP_GET_RANGE(array)                   \
    for(auto& t : (array)) {                    \
        if(t.syncMode!=syncMode) continue;      \
        startTimeNs = t.getStartTimeNs();       \
        timeRangeNs = t.getTimeRangeNs();       \
        return;                                 \
    }

    // Set default
    startTimeNs = 0;
    timeRangeNs = _record->durationNs;
    // Find the first group matching range
    LOOP_GET_RANGE(_timelines);
    LOOP_GET_RANGE(_memTimelines);
    LOOP_GET_RANGE(_plots);
}


void
vwMain::synchronizeNewRange(int syncMode, s64 startTimeNs, s64 timeRangeNs)
{
    if(syncMode<=0) return; // Source is not synchronized
    if(startTimeNs<0) startTimeNs = 0;
    if(_record && timeRangeNs>_record->durationNs) timeRangeNs = _record->durationNs;

#define LOOP_SET_RANGE(array)                   \
    for(auto& t : (array)) {                    \
        if(t.syncMode!=syncMode) continue;      \
        t.setView(startTimeNs, timeRangeNs);    \
    }

    LOOP_SET_RANGE(_timelines);
    LOOP_SET_RANGE(_memTimelines);
    LOOP_SET_RANGE(_plots);
}


void
vwMain::ensureThreadVisibility(int syncMode, int threadId)
{
    if(syncMode<=0) return; // Source is not synchronized
#define LOOP_VISIBILITY(array)                  \
    for(auto& t : (array)) {                    \
        if(t.syncMode!=syncMode) continue;      \
        t.ensureThreadVisibility(threadId);     \
    }

    LOOP_VISIBILITY(_timelines);
    LOOP_VISIBILITY(_memTimelines);
}


void
vwMain::synchronizeText(int syncMode, int threadId, int level, u32 lIdx, s64 timeNs, u32 idToIgnore)
{
    if(syncMode<=0) return; // Source is not synchronized

    // Text windows
    for(auto& tw : _texts) {
        if(tw.syncMode==syncMode && tw.threadId==threadId) {
            // Ensure that nestingLevel and lIdx are correct
            if(lIdx==PL_INVALID) {
                cmGetRecordPosition(_record, threadId, timeNs, level, lIdx);
            }
            // Set the position
            tw.setStartPosition(level, lIdx, idToIgnore);
            tw.didUserChangedScrollPosExt = true;
        }
    }

    // Log windows
    for(auto& lv : _logViews) {
        if(lv.syncMode==syncMode) {
            lv.setStartPosition(timeNs, idToIgnore);
        }
    }

    // Search window
    if(_search.syncMode==syncMode) {
        _search.setStartPosition(timeNs, idToIgnore);
    }
}


void
vwMain::synchronizeThreadLayout(void) // Invalidate the cache
{
    for(auto& t : _timelines)    t.isCacheDirty = true;
    for(auto& t : _memTimelines) t.isCacheDirty = true;
}


// Contextual menu helpers
// =======================

void
vwMain::prepareGraphContextualMenu(int elemIdx, s64 startTimeNs, s64 timeRangeNs, bool addAllNames, bool withRemoval)
{
    // Build the menu if not done already
    if(!_plotMenuItems.empty()) return;

    _plotMenuNewPlotUnits.clear();
    _plotMenuNewPlotCount.clear();
    _plotMenuWithRemoval = withRemoval;
    _plotMenuNamesWidth = 0.;
    _plotMenuAddAllNames = addAllNames;
    _plotMenuHasScopeChildren = false;
    _plotMenuLogParamQty = 0;

    // Get plot and its unit
    if(elemIdx<0) return;
    cmRecord::Elem& elem  = _record->elems[elemIdx];
    bsString unit = _record->getString(elem.nameIdx).unit;
    if(unit.empty()) unit = getUnitFromFlags(elem.flags);
    _plotMenuIsPartOfHStruct = elem.isPartOfHStruct;

    // Get the graph name
    char name[256];
    if(unit.empty()) snprintf(name, sizeof(name), "%s",      _record->getString(elem.nameIdx).value.toChar());
    else             snprintf(name, sizeof(name), "%s (%s)", _record->getString(elem.nameIdx).value.toChar(), unit.toChar());

    // Get all the matching existing plot windows, which do not already contain the elemIdx
    bsVec<int> matchingPwIdxs;
    for(int pwIdx=0; pwIdx<_plots.size(); ++pwIdx) {
        if(_plots[pwIdx].unit!=unit) continue;
        bool isPresent = false;
        for(const vwMain::PlotCurve& c : _plots[pwIdx].curves) if(c.elemIdx==elemIdx) isPresent = true;
        if(!isPresent) matchingPwIdxs.push_back(pwIdx);
    }

    // Add to the ctx menu
    _plotMenuThreadUniqueHash = (elem.threadId>=0)? _record->threads[elem.threadId].threadUniqueHash : 0;
    _plotMenuItems.push_back( { name, unit, elemIdx, elem.nameIdx, elem.flags, matchingPwIdxs, startTimeNs, timeRangeNs } );
}


void
vwMain::prepareGraphLogContextualMenu(int elemIdx, s64 startTimeNs, s64 timeRangeNs, bool withRemoval)
{
    // Build the menu if not done already
    if(!_plotMenuItems.empty()) return;

    _plotMenuNewPlotUnits.clear();
    _plotMenuNewPlotCount.clear();
    _plotMenuWithRemoval = withRemoval;
    _plotMenuNamesWidth = 0.;
    _plotMenuAddAllNames = false;
    _plotMenuHasScopeChildren = false;
    _plotMenuIsPartOfHStruct = false;

    // Get plot and its unit
    if(elemIdx<0) return;
    cmRecord::Elem& elem  = _record->elems[elemIdx];
    if(elem.flags!=PL_FLAG_TYPE_LOG) return;  // Sanity

    cmRecordIteratorLog it(_record, elemIdx, 0, 0.);
    char name[256];
    cmRecord::Evt evt;
    bool isCoarse;
    bsVec<cmLogParam> params;
    if(!it.getNextLog(isCoarse, evt, params) || params.empty()) return;  // At least one required for graphs
    _plotMenuLogParamQty = params.size();

    for(int paramIdx=0; paramIdx<_plotMenuLogParamQty; ++paramIdx) {
        bsString unit = getUnitFromFlags(params[paramIdx].paramType);

        // Get all the matching existing plot windows, which do not already contain the elemIdx
        bsVec<int> matchingPwIdxs;
        for(int pwIdx=0; pwIdx<_plots.size(); ++pwIdx) {
            if(_plots[pwIdx].unit!=unit) continue;
            bool isPresent = false;
            for(const vwMain::PlotCurve& c : _plots[pwIdx].curves) if(c.elemIdx==elemIdx && c.logParamIdx==paramIdx) isPresent = true;
            if(!isPresent) matchingPwIdxs.push_back(pwIdx);
        }

        // Add to the ctx menu
        snprintf(name, sizeof(name), "Parameter #%d", paramIdx);
        _plotMenuThreadUniqueHash = (elem.threadId>=0)? _record->threads[elem.threadId].threadUniqueHash : 0;
        _plotMenuItems.push_back( { name, unit, elemIdx, elem.nameIdx, elem.flags, matchingPwIdxs, startTimeNs, timeRangeNs, paramIdx } );
        _plotMenuNamesWidth = bsMax(_plotMenuNamesWidth, ImGui::CalcTextSize(name).x);
    }
}


bool
vwMain::prepareGraphContextualMenu(int threadId, int nestingLevel, u32 lIdx, s64 startTimeNs, s64 timeRangeNs,
                                   bool withChildren, bool withRemoval)
{
    // Build the menu if not done already
    if(!_plotMenuItems.empty()) return true;

    _plotMenuNewPlotUnits.clear();
    _plotMenuNewPlotCount.clear();
    _plotMenuWithRemoval = withRemoval;
    _plotMenuNamesWidth = 0.;
    _plotMenuAddAllNames = true;
    _plotMenuHasScopeChildren = false;
    _plotMenuIsPartOfHStruct = true;
    _plotMenuLogParamQty = 0;

    // Get parent
    bsVec<cmRecordIteratorHierarchy::Parent> parents;
    cmRecordIteratorHierarchy it(_record, threadId, nestingLevel, lIdx);
    it.getParents(parents);
    plAssert(!parents.empty(), "At least current item is expected");
    if(parents[0].evt.flags==PL_FLAG_TYPE_LOCK_RELEASED) parents[0].evt.flags = PL_FLAG_TYPE_LOCK_ACQUIRED;  // Replace the lock end by the lock begin

    // Compute scope hashpath in reverse order
    _plotMenuThreadUniqueHash = (threadId>=0)? _record->threads[threadId].threadUniqueHash : 0;
    u64 hashPath = bsHashStep(cmConst::SCOPE_NAMEIDX);
    for(int i=parents.size()-1; i>=0; --i) {
        hashPath = bsHashStep(_record->getString(parents[i].evt.nameIdx).hash, hashPath);
    }

    // Get children if it is a scope
    _workDataChildren.clear();
    _workLIdxChildren.clear();
    if(withChildren && (parents[0].evt.flags&PL_FLAG_SCOPE_BEGIN)) {
        cmRecordIteratorScope itc(_record, threadId, nestingLevel, lIdx);
        itc.getChildren(parents[0].evt.linkLIdx, lIdx, false, true, true, _workDataChildren, _workLIdxChildren);
        _plotMenuHasScopeChildren = itc.wasAScopeChildSeen();
    }

    auto addPlotMenuItem =
        [this, startTimeNs, timeRangeNs](const cmRecord::Evt& e, u64 itemHashPath) -> bool {
            // Get the unit
            bsString unit = _record->getString(e.nameIdx).unit;
            if(unit.empty()) unit = getUnitFromFlags(e.flags);

            // Get name and path
            char name[256];
            if(unit.empty()) snprintf(name, sizeof(name), "%s",      _record->getString(e.nameIdx).value.toChar());
            else             snprintf(name, sizeof(name), "%s (%s)", _record->getString(e.nameIdx).value.toChar(), unit.toChar());
            int* elemIdx = _record->elemPathToId.find(itemHashPath, e.nameIdx);
            if(!elemIdx || !_record->elems[*elemIdx].isPartOfHStruct) return false;

            // Get all the matching existing plot windows, which do not already contain the elemIdx
            bsVec<int> existingPlotWindowIndices;
            for(int plotWindowIdx=0; plotWindowIdx<_plots.size(); ++ plotWindowIdx) {
                if(_plots[plotWindowIdx].unit!=unit) continue;
                bool isPresent = false;
                for(const vwMain::PlotCurve& c : _plots[plotWindowIdx].curves) { if(c.elemIdx==*elemIdx) isPresent = true; }
                if(!isPresent) existingPlotWindowIndices.push_back(plotWindowIdx);
            }
            // Add
            _plotMenuItems.push_back( { name, unit, *elemIdx, e.nameIdx, e.flags, existingPlotWindowIndices, startTimeNs, timeRangeNs } );
            _plotMenuNamesWidth = bsMax(_plotMenuNamesWidth, ImGui::CalcTextSize(name).x);
            return true;
        };

    // Add the hovered item
    _plotMenuItems.reserve(1+_workDataChildren.size());
    u64 itemHashPath = bsHashStep(parents[0].evt.flags, hashPath);
    itemHashPath     = bsHashStep(_record->threads[threadId].threadHash, itemHashPath);    // Finish the hash with the thread part
    if(!addPlotMenuItem(parents[0].evt, itemHashPath)) return false; // Root item shall be plotable

    // Add the item children to the potential plot list
    _plotMenuNamesWidth = 0.; // For children only
    bsVec<u64> plotUniqueHashes; // In order to remove duplicates
    for(const cmRecord::Evt& evt : _workDataChildren) {
        // Skip scopes (only flat ones) and lock notifications (because the ones inside the hierarchical tree are not suitable for plot/histo)
        if(evt.flags&PL_FLAG_SCOPE_MASK) continue;
        if((evt.flags&PL_FLAG_TYPE_MASK)==PL_FLAG_TYPE_LOCK_NOTIFIED) continue;
        // Compute the path
        u64  childHashPath = bsHashStep(_record->getString(evt.nameIdx).hash, hashPath);
        childHashPath      = bsHashStep(evt.flags, childHashPath);
        childHashPath      = bsHashStep(_record->threads[threadId].threadHash, childHashPath);    // Finish the hash with the thread part
        // Already present?
        bool isAlreadyPresent = false;
        for(u64 h : plotUniqueHashes) if(h==childHashPath) { isAlreadyPresent = true; break; }
        if(isAlreadyPresent) continue;

        // Add the item
        addPlotMenuItem(evt, childHashPath);
        plotUniqueHashes.push_back(childHashPath);
    }

    return true;
}


bool
vwMain::displayPlotContextualMenu(int threadId, const char* rootText, float headerWidth, float comboWidth)
{
    if(comboWidth<=0.) comboWidth = ImGui::CalcTextSize("New plot #OOOOO").x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    char tmpStr[64];
    bool rootPlotSelected     = false;
    bool innerFieldsSelected  = false;
    bool innerFieldsDisplayed = false;
    // Display the list of plottable items
    for(int i=0; i<_plotMenuItems.size(); ++i) {
        auto& pmi = _plotMenuItems[i];

        // Structured menu
        if((_plotMenuLogParamQty==0 && i==1) || (_plotMenuLogParamQty!=0 && i==0)) {
            if(!ImGui::BeginMenu(_plotMenuLogParamQty? "Plot log parameters" : "Plot inner fields")) break; // No need to display inner fields
            innerFieldsDisplayed = true;
        }
        ImGui::PushID(i);

        // Display the item names
        if(_plotMenuLogParamQty==0 && i==0) {
            ImGui::Text("%s", rootText);
            ImGui::SameLine((headerWidth>0.f)? headerWidth : ImGui::GetWindowContentRegionMax().x-comboWidth-spacing);
        } else {
            ImGui::Text("%s", pmi.name.toChar());
            ImGui::SameLine(2.f*spacing+_plotMenuNamesWidth);
        }

        // Build the choices of the combo box for this plot, depending on its unit
        ImGui::SetNextItemWidth(comboWidth);
        float cursorX = ImGui::GetCursorPosX();
        if(ImGui::BeginCombo("", pmi.comboSelectionString.toChar(), 0)) {
            // None
            bool isSelected = (pmi.comboSelectionExistingIdx==-1 && pmi.comboSelectionNewIdx==-1);
            if(ImGui::Selectable("-", isSelected)) {
                if(pmi.comboSelectionNewIdx>=0) _plotMenuNewPlotCount[pmi.comboSelectionNewIdx] -= 1;
                pmi.comboSelectionExistingIdx = pmi.comboSelectionNewIdx = -1;
                pmi.comboSelectionRemoval = false;
                pmi.comboSelectionString.clear();
            }
            if(isSelected) ImGui::SetItemDefaultFocus();
            // List of existing plots
            for(int j=0; j<pmi.existingPlotWindowIndices.size(); ++j) {
                int pwi = pmi.existingPlotWindowIndices[j];
                const vwMain::PlotWindow& pw = _plots[pwi];
                if(pw.unit!=pmi.unit) continue;
                isSelected = (pmi.comboSelectionExistingIdx==j);
                snprintf(tmpStr, sizeof(tmpStr), "Plot #%d", pw.uniqueId);
                if(ImGui::Selectable(tmpStr, isSelected)) {
                    if(pmi.comboSelectionNewIdx>=0) _plotMenuNewPlotCount[pmi.comboSelectionNewIdx] -= 1;
                    pmi.comboSelectionNewIdx      = -1;
                    pmi.comboSelectionExistingIdx = j;
                    pmi.comboSelectionRemoval     = false;
                    pmi.comboSelectionString      = tmpStr;
                    if(_plotMenuLogParamQty==0 && i==0) rootPlotSelected = true;
                }
                if(isSelected) ImGui::SetItemDefaultFocus();
            }
            // List of new plots
            bool doAllowCreate = true;
            for(int j=0; j<_plotMenuNewPlotUnits.size(); ++j) {
                if(_plotMenuNewPlotUnits[j]!=pmi.unit) continue;
                if(_plotMenuNewPlotCount[j]==0)        continue;
                isSelected = (pmi.comboSelectionNewIdx==j);
                snprintf(tmpStr, sizeof(tmpStr), "New plot (%c)", 'A'+bsMin(j, 25));
                if(ImGui::Selectable(tmpStr, isSelected)) {
                    if(pmi.comboSelectionNewIdx>=0) _plotMenuNewPlotCount[pmi.comboSelectionNewIdx] -= 1;
                    _plotMenuNewPlotCount[j] += 1;
                    pmi.comboSelectionNewIdx      = j;
                    pmi.comboSelectionExistingIdx = -1;
                    pmi.comboSelectionRemoval     = false;
                    pmi.comboSelectionString      = tmpStr;
                    if(_plotMenuLogParamQty==0 && i==0) rootPlotSelected = true;
                }
                if(isSelected) {
                    ImGui::SetItemDefaultFocus();
                    if(_plotMenuNewPlotCount[j]<=1) doAllowCreate = false; // Already an independent one...
                }
            }
            // Create a new independent plot
            if(doAllowCreate && ImGui::Selectable("New plot", false)) {
                // Get the new index, reusing empty ones if any
                int newIdx = _plotMenuNewPlotUnits.size();
                for(int k=0; k<_plotMenuNewPlotCount.size(); ++k) {
                    if(_plotMenuNewPlotCount[k]==0) { newIdx = k; break; }
                }
                if(newIdx==_plotMenuNewPlotUnits.size()) {
                    _plotMenuNewPlotUnits.push_back(pmi.unit);
                    _plotMenuNewPlotCount.push_back(0);
                }
                _plotMenuNewPlotCount[newIdx] += 1;
                snprintf(tmpStr, sizeof(tmpStr), "New plot (%c)", 'A'+bsMin(newIdx, 25));
                pmi.comboSelectionNewIdx      = newIdx;
                pmi.comboSelectionExistingIdx = -1;
                pmi.comboSelectionRemoval     = false;
                pmi.comboSelectionString      = tmpStr;
                if(_plotMenuLogParamQty==0 && i==0) rootPlotSelected = true;
            }
            // Remove the plot
            if(_plotMenuWithRemoval && ImGui::Selectable("Remove", pmi.comboSelectionRemoval)) {
                if(pmi.comboSelectionNewIdx>=0) _plotMenuNewPlotCount[pmi.comboSelectionNewIdx] -= 1;
                pmi.comboSelectionNewIdx      = -1;
                pmi.comboSelectionExistingIdx = -1;
                pmi.comboSelectionRemoval     = true;
                pmi.comboSelectionString      = "Remove";
                if(_plotMenuLogParamQty==0 && i==0) rootPlotSelected = true;
            }

            ImGui::EndCombo();
        } // End of plot combo selection

        ImGui::SameLine(cursorX+comboWidth+spacing); // Make position independent of the choice plot checkbox/combobox
        ImGui::NewLine();
        ImGui::PopID();

    }

    // Ends the inner field sub menu
    if(innerFieldsDisplayed) {
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x-ImGui::CalcTextSize("Apply").x-2.f*spacing);
        innerFieldsSelected = ImGui::Button("Apply##Plot");
        ImGui::EndMenu();
    }

    // Apply the choices
    if(innerFieldsSelected || rootPlotSelected) {
        if(_plotMenuLogParamQty==0) {
            // Some exclusive cleaning of the "other" selection
            plAssert(innerFieldsSelected^rootPlotSelected);
            for(int i=rootPlotSelected?1:0; i<(rootPlotSelected?_plotMenuItems.size():1); ++i) {
                auto& pmi = _plotMenuItems[i];
                if(pmi.comboSelectionNewIdx>=0) _plotMenuNewPlotCount[pmi.comboSelectionNewIdx] -= 1;
                pmi.comboSelectionNewIdx      = -1;
                pmi.comboSelectionExistingIdx = -1;
                pmi.comboSelectionRemoval     = false;
            }
        }

        // Create the non empty new plot windows
        bsVec<int> realIdxLkup(_plotMenuNewPlotCount.size());
        for(int j=0; j<_plotMenuNewPlotCount.size(); ++j) {
            if(_plotMenuNewPlotCount[j]==0) { realIdxLkup[j] = -1; continue; }
            realIdxLkup[j] = _plots.size();
            _plots.push_back( { } );
            auto& pw       = _plots.back();
            pw.uniqueId    = getId();
            pw.unit        = _plotMenuNewPlotUnits[j];
            pw.startTimeNs = _plotMenuItems[0].startTimeNs;
            pw.timeRangeNs = _plotMenuItems[0].timeRangeNs;
            setFullScreenView(-1);
        }

        // Loop on potential plots
        u64 threadUniqueHash = (threadId>=0)? _record->threads[threadId].threadUniqueHash : 0;
        for(auto& pmi : _plotMenuItems) {
            // Case insertion in existing plot window
            if(pmi.comboSelectionExistingIdx>=0) {
                plAssert(pmi.comboSelectionNewIdx==-1 && !pmi.comboSelectionRemoval);
                int plotWindowIdx = pmi.existingPlotWindowIndices[pmi.comboSelectionExistingIdx];
                plAssert(plotWindowIdx<_plots.size());
                _plots[plotWindowIdx].isCacheDirty = true;
                _plots[plotWindowIdx].valueMin = +1e300; // Resets the displayed scale
                _plots[plotWindowIdx].valueMax = -1e300;
                if(_plotMenuAddAllNames) {
                    for(int elemIdx=0; elemIdx<_record->elems.size(); ++elemIdx) {
                        const cmRecord::Elem& elem = _record->elems[elemIdx];
                        if((bool)elem.isPartOfHStruct==_plotMenuIsPartOfHStruct && elem.threadId==threadId && elem.nameIdx==pmi.nameIdx && elem.flags==pmi.flags) {
                            bool isPresent = false;  // Need to check if already present due to the "all names"
                            for(const vwMain::PlotCurve& c : _plots[plotWindowIdx].curves) if(c.elemIdx==elemIdx) isPresent = true;
                            if(!isPresent) _plots[plotWindowIdx].curves.push_back( { threadUniqueHash, elem.partialHashPath, elemIdx, true, false } );
                        }
                    }
                }
                else _plots[plotWindowIdx].curves.push_back( { threadUniqueHash, _record->elems[pmi.elemIdx].partialHashPath, pmi.elemIdx, true, false, pmi.logParamIdx } );
            }
            // Case creation of a new plot window
            else if(pmi.comboSelectionNewIdx>=0) {
                plAssert(!pmi.comboSelectionRemoval);
                int plotWindowIdx = realIdxLkup[pmi.comboSelectionNewIdx];
                plAssert(plotWindowIdx>=0);
                plAssert(plotWindowIdx<_plots.size());
                if(_plotMenuAddAllNames) {
                    for(int elemIdx=0; elemIdx<_record->elems.size(); ++elemIdx) {
                        const cmRecord::Elem& elem = _record->elems[elemIdx];
                        if((bool)elem.isPartOfHStruct==_plotMenuIsPartOfHStruct && elem.threadId==threadId && elem.nameIdx==pmi.nameIdx && elem.flags==pmi.flags) {
                            _plots[plotWindowIdx].curves.push_back( { threadUniqueHash, elem.partialHashPath, elemIdx, true, false } );
                        }
                    }
                }
                else _plots[plotWindowIdx].curves.push_back( { threadUniqueHash, _record->elems[pmi.elemIdx].partialHashPath, pmi.elemIdx, true, false, pmi.logParamIdx } );
            }
        }
        plLogInfo("user", "Add plot(s)");
        return false; // Closes the window
    } // End of case of adding plots

    // Do not close the window
    return true;
}


bool
vwMain::displayHistoContextualMenu(float headerWidth, float comboWidth)
{
    bool isFullRange = (!_plotMenuItems.empty() && _plotMenuItems[0].startTimeNs==0 && _plotMenuItems[0].timeRangeNs==_record->durationNs);
    if(comboWidth<=0.) comboWidth = ImGui::CalcTextSize("New plot #OOOOO").x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    bool rootHistoSelected    = false;
    bool innerFieldsSelected  = false;
    bool innerFieldsDisplayed = false;
    ImGui::PushID("HistoMenu");

    // Display the list of plottable items
    for(int i=0; i<_plotMenuItems.size(); ++i) {
        auto& pmi = _plotMenuItems[i];

        // Structured menu
        if((_plotMenuLogParamQty==0 && i==1) || (_plotMenuLogParamQty!=0 && i==0)) {
            if(!ImGui::BeginMenu(_plotMenuLogParamQty? "Histo of log parameters" : "Histo of inner fields")) break; // No need to display inner fields
            innerFieldsDisplayed = true;
        }
        ImGui::PushID(0x700000+i);

        // Display the item names
        if(_plotMenuLogParamQty==0 && i==0) {
            ImGui::Text("Histogram");
            ImGui::SameLine((headerWidth>0.)? headerWidth : ImGui::GetWindowContentRegionMax().x-comboWidth-spacing);
        } else {
            ImGui::Text("%s", pmi.name.toChar());
            ImGui::SameLine(2.f*spacing+_plotMenuNamesWidth);
        }

        ImGui::SetNextItemWidth(comboWidth);
        float cursorX = ImGui::GetCursorPosX();
        if(ImGui::BeginCombo("", pmi.comboHistoSelectionString.toChar(), 0)) {
            // Empty
            if(ImGui::Selectable("-", false)) {
                pmi.comboHistoSelectionString.clear();
                pmi.comboHistoSelectionIdx = -1;
            }
            // Full range
            bool isSelected = (pmi.comboHistoSelectionIdx==0);
            if(ImGui::Selectable("Full record", isSelected)) {
                pmi.comboHistoSelectionString = "Full record";
                pmi.comboHistoSelectionIdx = 0;
                if(_plotMenuLogParamQty==0 && i==0) rootHistoSelected = true;
            }
            if(isSelected) ImGui::SetItemDefaultFocus();
            // Visible range (only if not full range)
            if(!isFullRange) {
                isSelected = (pmi.comboHistoSelectionIdx==1);
                if(ImGui::Selectable("Only visible", isSelected)) {
                    pmi.comboHistoSelectionString = "Only visible";
                    pmi.comboHistoSelectionIdx = 1;
                    if(_plotMenuLogParamQty==0 && i==0) rootHistoSelected = true;
                }
                if(isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        } // End of histogram combo selection

        ImGui::SameLine(cursorX+comboWidth+spacing); // Make position independent of the choice plot checkbox/combobox
        ImGui::NewLine();
        ImGui::PopID();
    }

    // Ends the inner field sub menu
    if(innerFieldsDisplayed) {
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x-ImGui::CalcTextSize("Apply").x-2.f*spacing);
        innerFieldsSelected = ImGui::Button("Apply##Histo");
        ImGui::EndMenu();
    }

    // Apply the choices
    if(innerFieldsSelected || rootHistoSelected) {
        if(_plotMenuLogParamQty==0) {
            // Some exclusive cleaning of the "other" selection
            plAssert(innerFieldsSelected^rootHistoSelected);
            for(int i=rootHistoSelected?1:0; i<(rootHistoSelected?_plotMenuItems.size():1); ++i) {
                _plotMenuItems[i].comboHistoSelectionIdx = -1;
            }
        }

        // Create new histograms
        for(auto& pmi : _plotMenuItems) {
            const cmRecord::Elem& elem = _record->elems[pmi.elemIdx];
            if     (pmi.comboHistoSelectionIdx==0) addHistogram(getId(), _plotMenuThreadUniqueHash, elem.partialHashPath, pmi.elemIdx, 0, _record->durationNs, pmi.logParamIdx);
            else if(pmi.comboHistoSelectionIdx==1) addHistogram(getId(), _plotMenuThreadUniqueHash, elem.partialHashPath, pmi.elemIdx, pmi.startTimeNs, pmi.timeRangeNs, pmi.logParamIdx);
        }
    }

    // Return
    ImGui::PopID();
    return !(innerFieldsSelected || rootHistoSelected); // False (= close window) if "apply" called
}


void
vwMain::displayColorSelectMenu(const char* title, const int colorIdx, std::function<void(int)>& setter)
{
    static int initialColorIdx = -1;
    const bsVec<ImVec4>& palette = getConfig().getColorPalette();
    constexpr ImGuiColorEditFlags colorButtonFlags = (ImGuiColorEditFlags_NoAlpha   | ImGuiColorEditFlags_NoPicker |
                                                      ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop);

    // Menu entry
    ImGui::BeginGroup();
    ImGui::Selectable(title, false, ImGuiSelectableFlags_DontClosePopups); ImGui::SameLine(0., 20.);
    ImGui::ColorButton("##color", palette[colorIdx], colorButtonFlags,
                       ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
    ImGui::EndGroup();
    if(ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
        ImGui::OpenPopup("Color palette");
        initialColorIdx = colorIdx;
    }

    // Popup
    if(ImGui::BeginPopup("Color palette", ImGuiWindowFlags_AlwaysAutoResize)) {
        // Current color
        ImGui::BeginGroup();
        ImGui::ColorButton("##color", palette[initialColorIdx], colorButtonFlags); ImGui::SameLine(); ImGui::Text("Current");
        ImGui::EndGroup();
        if(ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) ImGui::CloseCurrentPopup();
        // Palette
        ImGui::Text("Select a color:");
        bool hoveredColor = false;
        for(int j=0; j<palette.size(); ++j) {
            ImGui::PushID(j);
            if(j&7) ImGui::SameLine(0.0, ImGui::GetStyle().ItemSpacing.y);
            if(ImGui::ColorButton("##color", palette[j], colorButtonFlags, ImVec2(20, 20))) {
                setter(j);
                initialColorIdx = -1;
                plLogInfo("user", "Change one color");
                ImGui::CloseCurrentPopup();
            }
            else if(ImGui::IsItemHovered()) {
                setter(j);
                hoveredColor = true;
            }
            ImGui::PopID();
        }
        // Cancel hovered color
        if(!hoveredColor && initialColorIdx>=0) {
            setter(initialColorIdx);
        }
        ImGui::EndPopup(); // End of "Color thread palette"
    }
    else {
        initialColorIdx = -1;
    }
}



// Display helpers
// ===============

// Formatted help message display. Limited but simple and enough for the need.
// Line starting with '-' is a bullet
// Line starting with '##' is a section title. If on the first line, it is centered.
// A line equal to "===" is an horizontal separator
// A chunk of line between two '#' is color highlighted
// A '|' in a line means a column separator (1 max per line). First column has a consistent width for the full text.
void
vwMain::displayHelpText(const char* helpStr)
{
    // First pass: identify columns and compute the width of the 1st column
    const char* s = helpStr;
    float columnWidth = 0;
    while(*s) {
        // Find the line
        const char* endS = s;
        while(*endS!=0 && *endS!='|' && *endS!='\n') ++endS;
        if(*endS==0) break;
        if(*endS=='|') columnWidth = bsMax(columnWidth, ImGui::CalcTextSize(s, endS).x);
        s = endS+1;
    }
    if(columnWidth>0.) columnWidth += ImGui::CalcTextSize("OOO").x; // Add some margin, especially for bullet

    // Second pass: real display
    s = helpStr;
    bool isFirstLine = true;
    while(*s) {
        // Find the line
        const char* endS = s;
        while(*endS!=0 && *endS!='\n') ++endS;
        if(*endS==0) break;

        // Get the type of line
        bool isTitle  = (*s=='#') && (*(s+1)=='#');
        bool isBullet = (*s=='-');
        if(isTitle ) s += 2;
        if(isBullet) s += 1;

        // Display
        if(isTitle) {
            ImGui::Spacing();
            if(isFirstLine) {
                bsString title(s, endS);
                float startX = 0.5f*(ImGui::GetWindowContentRegionMax().x-ImGui::CalcTextSize(title.toChar()).x);
                ImGui::SetCursorPosX(startX);
                ImGui::TextColored(vwConst::gold, "%s", title.toChar());
            }
            else {
                ImGui::TextColored(vwConst::gold, "%.*s", (int)(endS-s), s);
            }
            ImGui::Spacing();
        }
        // Empty line
        else if(endS==s) ImGui::NewLine();
        // Horizontal separator
        else if((int)(endS-s)==3 && s[0]=='=' && s[1]=='=' && s[2]=='=') ImGui::Separator();
        // Standard text
        else {
            // Find the column separator
            const char* endSC = s;
            while(endSC<endS && *endSC!='|') ++endSC;

            for(int col=0; col<2; ++col) {
                // 2 columnns = 2 chunks to display
                const char* s3    = (col==0)?     s : endSC+1;
                const char* endS3 = (col==0)? endSC : endS;
                if(s3>=endS3) break;

                bool isFirstWord = true;
                bool isUnderHighlight = false;
                while(s3<endS3) {
                    // Find the highlight marker
                    const char* endS2 = s3;
                    while(endS2<endS3 && *endS2!='#') ++endS2;
                    if(isFirstWord && col==0 && isBullet) { ImGui::BulletText("%.*s", (int)(endS2-s3), s3); isFirstWord = false; }
                    else if(s3<endS2) {
                        if(!(isFirstWord && col==0)) ImGui::SameLine();
                        if(isFirstWord && col==1)    ImGui::SetCursorPosX(columnWidth);
                        if(isUnderHighlight)         ImGui::TextColored(vwConst::cyan, "%.*s", (int)(endS2-s3), s3);
                        else                         ImGui::Text("%.*s", (int)(endS2-s3), s3);
                        isFirstWord = false;
                    }
                    isUnderHighlight = !isUnderHighlight;
                    s3 = endS2 + 1;
                } // End of loop on chunks to display
            } // End of loop on columns
        } // End of standard text display

        // Next line
        isFirstLine = false;
        s = endS+1;
    }
}


void
vwMain::openHelpTooltip(int uniqueId, const char* tooltipId)
{
    ImGui::OpenPopup(tooltipId);
    _uniqueIdHelp = uniqueId;
}


void
vwMain::displayHelpTooltip(int uniqueId, const char* tooltipId, const char* helpStr)
{
    if(ImGui::BeginPopup(tooltipId, ImGuiWindowFlags_AlwaysAutoResize)) {
        displayHelpText(helpStr);
        if(_uniqueIdHelp!=uniqueId && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(KC_H)) {
            ImGui::CloseCurrentPopup();
        }
        _uniqueIdHelp = -1;
        ImGui::EndPopup();
    }
    else if(_uniqueIdHelp==uniqueId) _uniqueIdHelp = -1;  // Help closed externally
}


void
vwMain::displayScopeTooltip(const char* titleStr, const bsVec<cmRecord::Evt>& dataChildren, const cmRecord::Evt& evt, s64 durationNs)
{
    // First pass to collect elems on children
    char  tmpStr[128];
    u32   allocQty=0, allocSize=0, deallocQty=0, deallocSize=0;
    int   dataQty     = 0;
    int   childrenQty = 0;
    s64   timeInChildrenNs   = 0;
    s64   lastChildStartTime = 0;
    struct ChildElems {
        u32 nameIdx;
        int qty;
        s64 timeSpentNs;
    };
    bsVec<ChildElems> childrenElems;
    bool isTruncated = (dataChildren.size()>=cmConst::CHILDREN_MAX);
    for(const auto& d : dataChildren) {
        if(d.flags&PL_FLAG_SCOPE_BEGIN) { // Store the date of a child scope
            lastChildStartTime = d.vS64;
            continue;
        }
        if(d.flags&PL_FLAG_SCOPE_END && lastChildStartTime!=0) { // End of a child scope: store it
            timeInChildrenNs += d.vS64-lastChildStartTime;
            ++childrenQty;
            int i=0;
            for(;i<childrenElems.size(); ++i) {
                ChildElems& ci = childrenElems[i];
                if(d.nameIdx==ci.nameIdx) {
                    ci.timeSpentNs += d.vS64-lastChildStartTime;
                    ++ci.qty;
                    break;
                }
            }
            if(i==childrenElems.size()) childrenElems.push_back( { d.nameIdx, 1, d.vS64-lastChildStartTime } );
            lastChildStartTime = 0;
            continue;
        }
        // Case memory: update stats
        int dType = d.flags&PL_FLAG_TYPE_MASK;
        if(dType==PL_FLAG_TYPE_ALLOC) {
            allocQty  += d.getMemCallQty();
            allocSize += d.getMemByteQty();
            continue;
        }
        if(dType==PL_FLAG_TYPE_DEALLOC) {
            deallocQty  += d.getMemCallQty();
            deallocSize += d.getMemByteQty();
            continue;
        }
        // Case non scope elem
        if(dType>=PL_FLAG_TYPE_DATA_QTY) continue;
        ++dataQty;
    }

    // Tooltip
    ImGui::BeginTooltip();
    ImGui::TextColored(vwConst::gold, "%s", titleStr);
    if(evt.lineNbr>0) {
        ImGui::Text("At line"); ImGui::SameLine();
        ImGui::TextColored(vwConst::grey, "%d", evt.lineNbr); ImGui::SameLine();
        ImGui::Text("in file"); ImGui::SameLine();
    } else {
        ImGui::Text("In"); ImGui::SameLine();
    }
    ImGui::TextColored(vwConst::grey, "%s", _record->getString(evt.filenameIdx).value.toChar());
    int eType = evt.flags&PL_FLAG_TYPE_MASK;
    if(eType==PL_FLAG_TYPE_DATA_TIMESTAMP || (eType>=PL_FLAG_TYPE_WITH_TIMESTAMP_FIRST && eType<=PL_FLAG_TYPE_WITH_TIMESTAMP_LAST)) {
        ImGui::Text("At time"); ImGui::SameLine(); ImGui::TextColored(vwConst::grey, "%s", getNiceTime(evt.vS64, 0));
    }
    if(isTruncated) {
        ImGui::TextColored(vwConst::red, "(Truncated data, too much children)");
    }
    if(allocQty>0 || deallocQty>0) ImGui::Separator();
    if(allocQty) {
        ImGui::TextColored(vwConst::grey, "+%s", getNiceBigPositiveNumber(allocSize)); ImGui::SameLine();
        ImGui::Text("bytes in"); ImGui::SameLine();
        ImGui::TextColored(vwConst::grey, "%s", getNiceBigPositiveNumber(allocQty)); ImGui::SameLine();
        ImGui::Text("alloc calls");
    }
    if(deallocQty) {
        ImGui::TextColored(vwConst::grey, "-%s", getNiceBigPositiveNumber(deallocSize)); ImGui::SameLine();
        ImGui::Text("bytes in"); ImGui::SameLine();
        ImGui::TextColored(vwConst::grey, "%s", getNiceBigPositiveNumber(deallocQty)); ImGui::SameLine();
        ImGui::Text("dealloc calls");
    }
    if(!childrenElems.empty()) {
        std::sort(childrenElems.begin(), childrenElems.end(), [](const ChildElems& a, const ChildElems& b)->bool { return a.timeSpentNs>b.timeSpentNs; });
        ImGui::Separator();
        ImGui::TextColored(vwConst::grey, "%.1f%%", 100.*(double)timeInChildrenNs/(double)durationNs); ImGui::SameLine();
        ImGui::Text("time spent in"); ImGui::SameLine();
        ImGui::TextColored(vwConst::grey, "%s", getNiceBigPositiveNumber(childrenQty)); ImGui::SameLine();
        ImGui::Text("child%s", (childrenQty>1)? "ren" : "");
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x*3.f, style.CellPadding.y));
        if(ImGui::BeginTable("##table1", 2, ImGuiTableFlags_SizingFixedFit)) {
            float barWidth = ImGui::CalcTextSize("1000.00 ns (100.0 %%)").x;
            for(const ChildElems& ci : childrenElems) {
                ImGui::TableNextColumn();
                ImGui::Text("%s",_record->getString(ci.nameIdx).value.toChar());
                if(ci.qty>1) {
                    ImGui::SameLine();
                    ImGui::TextColored(vwConst::grey, "(%dx)", ci.qty);
                }
                ImGui::TableNextColumn();
                float ratio = (float)((double)ci.timeSpentNs/(double)durationNs);
                snprintf(tmpStr, sizeof(tmpStr), "%s (%.1f %%)", getNiceDuration(ci.timeSpentNs), 100.f*ratio);
                ImGui::ProgressBar(ratio, ImVec2(barWidth,ImGui::GetTextLineHeight()), tmpStr);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    // Second pass to display
    if(dataQty) {
        constexpr int maxDataQty = 25;
        ImGui::Separator();
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x*3.f, style.CellPadding.y));
        if(ImGui::BeginTable("##table2", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            int dataCount = 0;
            for(const auto& d : dataChildren) {
                if(d.flags&PL_FLAG_SCOPE_MASK) continue;
                int dType = d.flags&PL_FLAG_TYPE_MASK;
                if(dType>=PL_FLAG_TYPE_DATA_QTY) continue;
                ImGui::TableNextColumn();
                if((++dataCount)==maxDataQty-4) { // Line limit reached?
                    ImGui::Text(". . . "); ImGui::TableNextColumn();
                    break;
                }
                else ImGui::Text("%s", _record->getString(d.nameIdx).value.toChar());
                ImGui::TableNextColumn();
                if(dType!=PL_FLAG_TYPE_DATA_NONE) ImGui::TextColored(vwConst::grey, "%s", getValueAsChar(d));
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    ImGui::EndTooltip();
}


void
vwMain::computeTickScales(const double valueRange, const int targetTickQty, double& scaleMajorTick, double& scaleMinorTick)
{
    // Compute the tick period
    scaleMajorTick = pow(10., int(log10(valueRange))-1);
    scaleMinorTick = scaleMajorTick;
    for(int i=0; i<5; ++i) {
        int tickQty = (int)(valueRange/scaleMajorTick);
        if(tickQty<targetTickQty) break;
        scaleMinorTick = scaleMajorTick;
        scaleMajorTick *= (i&1)? 2. : 5.;
    }
}


void
vwMain::drawSynchroGroupCombo(float comboWidth, int* syncModePtr)
{
    ImGui::PushItemWidth(comboWidth);
    if(ImGui::Combo("##Synchro", syncModePtr, "Isolated\0Group 1\0Group 2\0\0")) {
        plLogInfo("user", "Change synchro group");
    }
    ImGui::PopItemWidth();
    if(ImGui::IsItemHovered() && getLastMouseMoveDurationUs()>500000) {
        ImGui::SetTooltip("Defines how window time ranges are synchronized.\nWindows can be 'isolated' or belong to group 1 or 2");
    }
}


bool
vwMain::manageVisorAndRangeSelectionAndBarDrag(TimeRangeBase& trb,
                                                bool isWindowHovered, float mouseX, float mouseY, float winX, float winY, float winWidth, float winHeight,
                                                bool isBarHovered, float rbWidth, float rbStartPix, float rbEndPix)
{
    const double nsToPix = winWidth/trb.timeRangeNs;
    // Drag with middle button
    if(isWindowHovered && ImGui::IsMouseDragging(1)) {
        // Update the selected range
        trb.rangeSelStartNs = trb.getStartTimeNs() + (s64)((mouseX-winX-ImGui::GetMouseDragDelta(1).x)/nsToPix);
        trb.rangeSelEndNs   = trb.getStartTimeNs() + (s64)((mouseX-winX)/nsToPix);

        // Cancel case
        if(trb.rangeSelStartNs>=trb.rangeSelEndNs) {
            trb.rangeSelStartNs = 0;
            trb.rangeSelEndNs = 0;
        }

        // Drag on-going: display the selection box with transparency and range
        else {
            char tmpStr[128];
            float x1 = winX+(float)(nsToPix*(trb.rangeSelStartNs-trb.getStartTimeNs()));
            float x2 = winX+(float)(nsToPix*(trb.rangeSelEndNs-trb.getStartTimeNs()));
            constexpr float arrowSize = 4.f;
            // White background
            DRAWLIST->AddRectFilled(ImVec2(x1, winY), ImVec2(x2, winY+winHeight), IM_COL32(255,255,255,128));
            // Range line
            DRAWLIST->AddLine(ImVec2(x1, mouseY), ImVec2(x2, mouseY), vwConst::uBlack, 2.f);
            // Arrows
            DRAWLIST->AddLine(ImVec2(x1, mouseY), ImVec2(x1+arrowSize, mouseY-arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(x1, mouseY), ImVec2(x1+arrowSize, mouseY+arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(x2, mouseY), ImVec2(x2-arrowSize, mouseY-arrowSize), vwConst::uBlack, 2.f);
            DRAWLIST->AddLine(ImVec2(x2, mouseY), ImVec2(x2-arrowSize, mouseY+arrowSize), vwConst::uBlack, 2.f);
            // Text
            snprintf(tmpStr, sizeof(tmpStr), "{ %s }", getNiceDuration(trb.rangeSelEndNs-trb.rangeSelStartNs));
            ImVec2 tb = ImGui::CalcTextSize(tmpStr);
            float x3 = 0.5f*(x1+x2-tb.x);
            if(x3<x1)      DRAWLIST->AddRectFilled(ImVec2(x3, mouseY-tb.y-5.f), ImVec2(x1,      mouseY-5), IM_COL32(255,255,255,128));
            if(x3+tb.x>x2) DRAWLIST->AddRectFilled(ImVec2(x2, mouseY-tb.y-5.f), ImVec2(x3+tb.x, mouseY-5), IM_COL32(255,255,255,128));
            DRAWLIST->AddText(ImVec2(x3, mouseY-tb.y-5.f), vwConst::uBlack, tmpStr);
        }
    }

    // Drag ended: set the selected range view
    else if(isWindowHovered && trb.rangeSelEndNs>0.) {
        trb.rangeSelStartNs = bsMax(trb.rangeSelStartNs, 0LL);
        trb.setView(trb.rangeSelStartNs, trb.rangeSelEndNs-trb.rangeSelStartNs, true);
        trb.rangeSelStartNs = trb.rangeSelEndNs = 0;
        return  true;
    }

    // No range selection, then draw the vertical visor
    else {
        float x = winX + (float)((_mouseTimeNs-trb.startTimeNs)*nsToPix);
        DRAWLIST->AddLine(ImVec2(x, winY), ImVec2(x, winY+winHeight), vwConst::uYellow, 1.0f);
    }

    // Manage the view navigation through the timeline top bar
    if(trb.dragMode==BAR || (isBarHovered && !ImGui::GetIO().KeyCtrl && trb.ctxDraggedId<0 && trb.dragMode==NONE)) {
        if(ImGui::IsMouseDragging(2)) {
            if(bsAbs(ImGui::GetMouseDragDelta(2).x)>1.) {
                trb.setView(trb.getStartTimeNs()+(s64)(_record->durationNs*ImGui::GetMouseDragDelta(2).x/winWidth), trb.getTimeRangeNs());
                ImGui::ResetMouseDragDelta(2);
                trb.dragMode = BAR;
                return true;
            }
        }
        // Else just set the middle screen time if clicked outside of the bar
        else if(ImGui::IsMouseDown(0) && mouseX<winX+rbWidth && (mouseX<rbStartPix || mouseX>rbEndPix)) {
            trb.setView((s64)(_record->durationNs*(mouseX-winX)/rbWidth-0.5*trb.getTimeRangeNs()), trb.getTimeRangeNs());
            trb.dragMode = BAR;
            return true;
        }
        else trb.dragMode = NONE;
    }
    else if(trb.dragMode==BAR) trb.dragMode = NONE;

    return false;
}


void
vwMain::drawTimeRuler(float winX, float winY, float winWidth, float rulerHeight, s64 startTimeNs, s64 timeRangeNs,
                       int& syncMode, float& rbWidth, float& rbStartPix, float& rbEndPix)
{
    const float MIN_TICK_WIDTH_PIX = 10.f*getConfig().getFontSize(); // Correspond to a typical date display
    constexpr float MIN_VIEWBAR_WIDTH_PIX = 10.f;
    const float fontYSpacing     = 0.5f*ImGui::GetStyle().ItemSpacing.y;
    const float textPixMargin    = 3.f*fontYSpacing;
    const bool   isWindowHovered = ImGui::IsWindowHovered();
    const float rbHeight         = ImGui::GetTextLineHeightWithSpacing();
    const float rbInnerBarOffset = 4.f;
    const float comboWidth       = ImGui::CalcTextSize("Isolated XXX").x;
    const s64   recordDurationNs = _record->durationNs;
    if(timeRangeNs<=0) timeRangeNs = bsMax(recordDurationNs, 1LL);
    const double nsToPix          = (double)winWidth/(double)timeRangeNs;

    // Visible range bar
    rbWidth      = winWidth-comboWidth;
    double toPix = (rbWidth-3.)/recordDurationNs;
    float  viewBarWidthPix = bsMax(MIN_VIEWBAR_WIDTH_PIX, toPix*timeRangeNs);
    rbStartPix   = winX + bsMax((float)(toPix*(startTimeNs+0.5*timeRangeNs)) - 0.5f*viewBarWidthPix, 0.f);
    rbEndPix     = winX + bsMin((float)(toPix*(startTimeNs+0.5*timeRangeNs)) + 0.5f*viewBarWidthPix, rbWidth);
    DRAWLIST->AddRectFilled(ImVec2(winX, winY), ImVec2(winX+winWidth, winY+rbHeight), vwConst::uGrey);
    DRAWLIST->AddRectFilled(ImVec2(rbStartPix, winY+rbInnerBarOffset), ImVec2(rbEndPix  , winY+rbHeight-rbInnerBarOffset), vwConst::uGrey128);

    // Mark active ranges (text & memory)
    for(auto& tw: _texts) {
        ImColor colorThread = getConfig().getThreadColor(tw.threadId);
        colorThread.Value.w = 0.5f; // Make the bar slightly transparent to handle overlaps
        float x1 = winX+rbWidth*(float)((double)tw.firstTimeNs/(double)recordDurationNs);
        float x2 = bsMax(x1+2.f, winX+rbWidth*(float)((double)tw.lastTimeNs/(double)recordDurationNs));
        DRAWLIST->AddRectFilled(ImVec2(x1, winY+6.f), ImVec2(x2, winY+rbHeight-6.f), colorThread);
    }
    for(auto& mw: _memTimelines) {
        if(mw.allocBlockThreadId<0) continue;
        ImColor colorThread = getConfig().getThreadColor(mw.allocBlockThreadId);
        colorThread.Value.w = 0.5f; // Make the bar slightly transparent to handle overlaps
        float x1 = winX+rbWidth*(float)((double)mw.allocBlockStartTimeNs/(double)recordDurationNs);
        float x2 = bsMax(x1+2.f, winX+rbWidth*(float)((double)mw.allocBlockEndTimeNs/(double)recordDurationNs));
        DRAWLIST->AddRectFilled(ImVec2(x1, winY+6.f), ImVec2(x2, winY+rbHeight-6.f), colorThread);
    }

    // Draw background
    float rulerY = winY+rbHeight;
    DRAWLIST->AddRectFilled(ImVec2(winX, rulerY), ImVec2(winX+winWidth, rulerY+rulerHeight), vwConst::uBlack);

    // Compute the tick period
    double scaleMajorTick, scaleMinorTick;
    computeTickScales((double)timeRangeNs, bsMinMax((int)(winWidth/MIN_TICK_WIDTH_PIX), 1, 10), scaleMajorTick, scaleMinorTick);

    // Draw the minor ticks
    float pixTick = (float)(-nsToPix*fmod((double)startTimeNs, scaleMajorTick));
    while(pixTick<winWidth) {
        DRAWLIST->AddLine(ImVec2(winX+pixTick, rulerY+rulerHeight-7), ImVec2(winX+pixTick, rulerY+rulerHeight), vwConst::uWhite);
        pixTick += (float)(nsToPix*scaleMinorTick);
    }

    // Draw the major ticks
    s64 timeTick = (s64)(scaleMajorTick*floor(startTimeNs/scaleMajorTick));
    pixTick = (float)(nsToPix*(timeTick-startTimeNs));
    while(pixTick<winWidth) {
        DRAWLIST->AddLine(ImVec2(winX+pixTick, rulerY), ImVec2(winX+pixTick, rulerY+rulerHeight), vwConst::uWhite, 2.0f);
        DRAWLIST->AddText(ImVec2(winX+pixTick+textPixMargin, rulerY+fontYSpacing), vwConst::uWhite, getNiceTime(timeTick, (s64)scaleMajorTick));
        pixTick  += (float)(nsToPix*scaleMajorTick);
        timeTick += (s64)scaleMajorTick;
    }

    // Draw the rule outside
    DRAWLIST->AddRect(ImVec2(winX, winY), ImVec2(winX+winWidth, rulerY+rulerHeight), vwConst::uGrey64, 0.f, ImDrawCornerFlags_All, 2.f);

    // Draw the tooltip showing the range if hovered, else the current time
    if(isWindowHovered) {
        ImGui::SetTooltip("Range { %s } - %s -> %s", getNiceDuration(timeRangeNs),
                          getNiceTime(startTimeNs, timeRangeNs, 0),
						  getNiceTime(startTimeNs+timeRangeNs, timeRangeNs, 1));
    } else {
        char tmpStr[128];
        snprintf(tmpStr, sizeof(tmpStr),  "%s", getNiceTime(_mouseTimeNs, (s64)(0.02f*scaleMajorTick))); // x50 precision for the time
        DRAWLIST->AddText(ImVec2(winX+(float)(nsToPix*(_mouseTimeNs-startTimeNs))-0.5f*ImGui::CalcTextSize(tmpStr).x,
                                 winY+0.5f*rbInnerBarOffset), vwConst::uBlack, tmpStr);
    }

    // Synchronization groups
    ImGui::SetCursorPos(ImVec2(winWidth-comboWidth, 0));
    drawSynchroGroupCombo(comboWidth, &syncMode);
}


float
vwMain::getTimelineHeaderHeight(bool withGroupHeader, bool withThreadHeader)
{
    return ((withGroupHeader? 1.6f : 0.f)+(withThreadHeader? 1.3f : 0.f))*ImGui::GetTextLineHeightWithSpacing();
}


bool
vwMain::displayTimelineHeader(float yHeader, float yThreadAfterTimeline, int threadId, bool doDrawGroup, bool isDrag,
                              bool& isThreadHovered, bool& isGroupHovered)
{
    // Constants
    constexpr float vBandWidth   = 10.f;
    constexpr ImU32  groupColor   = IM_COL32(30, 64, 96, 255);
    constexpr ImU32  groupHColor  = IM_COL32(30, 64, 96, 128);
    constexpr ImU32  threadColor  = IM_COL32(30, 64, 64, 255);
    constexpr ImU32  threadHColor = IM_COL32(30, 64, 64, 128);
    constexpr ImU32  whiteHColor = IM_COL32(255, 255, 255, 128);
    const float fontHeight = ImGui::GetTextLineHeightWithSpacing();
    const float tgSide = 0.8f*fontHeight;
    const float ttSide = 0.6f*fontHeight;
    const float groupTitleHeight  = 1.6f*fontHeight;
    const float threadTitleHeight = 1.3f*fontHeight;
    const bool   isWindowHovered   = ImGui::IsWindowHovered();
    const float mouseX = ImGui::GetMousePos().x;
    const float mouseY = ImGui::GetMousePos().y;
    const float winX   = ImGui::GetWindowPos().x;
    const float fontSpacing       = 0.5f*ImGui::GetStyle().ItemSpacing.y;
    const float threadTitleMargin = 1.f*fontSpacing;
    const float textPixMargin     = 2.f*fontSpacing;
    bool isConfigChanged = false;

    // Get elems from the threadId
    const char* threadName = 0;
    const char* groupName  = 0;
    int groupNameIdx = -1;
    if(threadId>=0 && threadId<cmConst::MAX_THREAD_QTY) {
        threadName   = _record->getString(_record->threads[threadId].nameIdx).value.toChar();
        groupNameIdx = _record->threads[threadId].groupNameIdx;
        if(groupNameIdx>=0) groupName  = _record->getString(groupNameIdx).value.toChar();
    }
    else if(threadId==vwConst::LOCKS_THREADID)      threadName = "Locks & Resources";
    else if(threadId==vwConst::CORE_USAGE_THREADID) threadName = "Cores";
    float threadNameWidth = ImGui::CalcTextSize(threadName).x;

    bool isGroupExpanded = !doDrawGroup || getConfig().getGroupExpanded(groupNameIdx);
    bool isThreadTransparent = false;
    isThreadHovered = isGroupHovered = isDrag;
    if(!isDrag && isWindowHovered && mouseX>=winX && mouseX<=winX+_timelineHeaderWidth && mouseY>=yHeader) {
        isGroupHovered  = doDrawGroup && mouseY<=yHeader+groupTitleHeight;
        isThreadHovered = isGroupExpanded && ((!doDrawGroup && mouseY<yHeader+threadTitleHeight) ||
                                             (doDrawGroup && mouseY>yHeader+groupTitleHeight && mouseY<=yHeader+groupTitleHeight+threadTitleHeight));
        isThreadTransparent = isThreadHovered && mouseX<winX+_timelineHeaderWidth-ttSide-threadTitleMargin-threadNameWidth-10;
    }

    // Draw the group header
    // =====================
    if(doDrawGroup) {
        // Background bar
        DRAWLIST->AddRectFilled(ImVec2(winX+threadTitleMargin, yHeader+2.f*threadTitleMargin), ImVec2(winX+_timelineHeaderWidth, yHeader+groupTitleHeight),
                                isDrag? groupHColor:groupColor);
        if(isDrag) { // Highlight the dragging with a white border
            DRAWLIST->AddRect(ImVec2(winX+threadTitleMargin, yHeader+2.f*threadTitleMargin), ImVec2(winX+_timelineHeaderWidth, yHeader+groupTitleHeight),
                              vwConst::uWhite, 0.f, ImDrawCornerFlags_All, 2.f);
        }

        // Expansion state triangle
        float tX = winX+2.f*threadTitleMargin, tY = yHeader+0.5f*(groupTitleHeight-0.8f*fontHeight)+fontSpacing;
        if(isGroupExpanded) {
            DRAWLIST->AddTriangleFilled(ImVec2(tX, tY), ImVec2(tX+tgSide, tY), ImVec2(tX+0.5f*tgSide, tY+0.707f*tgSide),
                                        isDrag? whiteHColor:vwConst::uWhite);
        }  else {
            float tdX = 0.293f*tgSide, tdY = 0.2f*tgSide;
            DRAWLIST->AddTriangleFilled(ImVec2(tX+tdX, tY-tdY), ImVec2(tX+tgSide, tY+0.5f*tgSide-tdY), ImVec2(tX+tdX, tY+tgSide-tdY),
                                        isDrag? whiteHColor:vwConst::uWhite);
        }

        // Text
        plAssert(groupName);
        DRAWLIST->AddText(ImVec2(tX+tgSide+2.f*textPixMargin, yHeader+0.5f*(groupTitleHeight-fontHeight)+fontSpacing),
                          isDrag? whiteHColor:vwConst::uWhite, groupName);

        // Triangle interaction
        yHeader += groupTitleHeight;
        if(isGroupHovered && !isDrag && !ImGui::GetIO().KeyCtrl && mouseX<=tX+fontHeight+2.*textPixMargin+ImGui::CalcTextSize(groupName).x &&
           mouseY<=yHeader+groupTitleHeight && ImGui::IsMouseReleased(0)) {
            getConfig().setGroupExpanded(groupNameIdx, !isGroupExpanded);
            isConfigChanged = true;
        }
    }
    if(!isGroupExpanded || (isDrag && doDrawGroup)) return isConfigChanged;

    // Draw the thread header
    // ======================
    bool  isThreadVisible = getConfig().getThreadExpanded(threadId);
    float tX = winX+_timelineHeaderWidth-ttSide-threadTitleMargin, tY = yHeader+0.5f*(groupTitleHeight-fontHeight)+fontSpacing;
    if(!isThreadTransparent) {
        // Background bar
        float xStart = winX+threadTitleMargin+(groupName? 4.0f*threadTitleMargin+vBandWidth : 0);
        DRAWLIST->AddRectFilled(ImVec2(xStart, yHeader+2.f*threadTitleMargin), ImVec2(winX+_timelineHeaderWidth, yHeader+threadTitleHeight),
                                isThreadHovered? threadHColor : threadColor);
        if(isDrag) { // Highlight the dragging
            DRAWLIST->AddRect(ImVec2(xStart, yHeader+2.f*threadTitleMargin), ImVec2(winX+_timelineHeaderWidth, yHeader+threadTitleHeight),
                              vwConst::uWhite, 0.f, ImDrawCornerFlags_All, 2.f);
        }

        // Expansion state triangle
        if(isThreadVisible) {
            DRAWLIST->AddTriangleFilled(ImVec2(tX, tY), ImVec2(tX+ttSide, tY), ImVec2(tX+0.5f*ttSide, tY+0.707f*ttSide),
                                        isThreadHovered? whiteHColor:vwConst::uWhite);
        } else {
            float tdX = 0.293f*ttSide, tdY = 0.2f*ttSide;
            DRAWLIST->AddTriangleFilled(ImVec2(tX+tdX, tY-tdY), ImVec2(tX+ttSide, tY+0.5f*ttSide-tdY), ImVec2(tX+tdX, tY+ttSide-tdY),
                                        isThreadHovered? whiteHColor:vwConst::uWhite);
        }

        // Text
        DRAWLIST->AddText(ImVec2(tX-threadNameWidth-10.f, yHeader+0.5f*(threadTitleHeight-fontHeight)+fontSpacing),
                          isThreadHovered? whiteHColor:vwConst::uWhite, threadName);

        // Draw the vertical bar
        if(!isDrag && groupName) {
            DRAWLIST->AddRectFilled(ImVec2(winX+3.f*threadTitleMargin, yHeader), ImVec2(winX+3.f*threadTitleMargin+vBandWidth, yThreadAfterTimeline),
                                    isThreadHovered? groupHColor:groupColor);
        }
    }

    // Triangle interaction
    if(isThreadHovered && !isDrag && !ImGui::GetIO().KeyCtrl && mouseX>=tX-threadNameWidth-10 && mouseX<=tX+fontHeight && mouseY>=yHeader &&
       mouseY<=yHeader+fontHeight && ImGui::IsMouseReleased(0)) {
        getConfig().setThreadExpanded(threadId, !isThreadVisible);
        isConfigChanged = true;
    }

    return isConfigChanged;
}


void
vwMain::displayTimelineHeaderPopup(TimeRangeBase& trb, int tId, bool openAsGroup)
{
    ImGui::PushID(tId);
    ImGui::PushID("thread context menu");
    if(trb.ctxDoOpenContextMenu) {
        ImGui::OpenPopup(openAsGroup? "Group menu":"Thread menu");
        trb.ctxDoOpenContextMenu = false;
    }

    // Check that we are drawing a thread or group menu
    bool isMenuAThread    = true;
    bool areWeDrawingMenu = ImGui::BeginPopup("Thread menu", ImGuiWindowFlags_AlwaysAutoResize);
    if(!areWeDrawingMenu) {
        areWeDrawingMenu = ImGui::BeginPopup("Group menu", ImGuiWindowFlags_AlwaysAutoResize);
        isMenuAThread    = false;
    }
    if(!areWeDrawingMenu) { ImGui::PopID(); ImGui::PopID(); return; }

    // Part of the menu only for threads, not group
    if(isMenuAThread && tId<cmConst::MAX_THREAD_QTY) {
        // Draw the popup menu
        ImGui::TextColored(vwConst::grey, "%s", _record->getString(_record->threads[tId].nameIdx).value.toChar());
        ImGui::Separator();
        ImGui::Separator();

        // Text menu
        if(ImGui::MenuItem("View as text")) { addText(getId(), tId); ImGui::CloseCurrentPopup(); }

        // Profiling menu
#define ADD_PROFILE(kind, startNs_, durationNs_) { addProfileRange(getId(), vwMain::kind, tId, 0, startNs_, durationNs_); ImGui::CloseCurrentPopup(); }
        bool isFullRange = (trb.startTimeNs==0 && trb.timeRangeNs==_record->durationNs);
        if(isFullRange) {
            if(ImGui::MenuItem("Profile timings")) ADD_PROFILE(TIMINGS, 0, _record->durationNs);
        }
        else {
            if(ImGui::BeginMenu("Profile timings")) {
                if(ImGui::MenuItem("Full thread"   )) ADD_PROFILE(TIMINGS, 0, _record->durationNs);
                if(ImGui::MenuItem("Visible region")) ADD_PROFILE(TIMINGS, trb.getStartTimeNs(), trb.getTimeRangeNs());
                ImGui::EndMenu();
            }
        }

        // Memory menu
        if(_record->threads[tId].memEventQty>0) {
            if(isFullRange) {
                if(ImGui::MenuItem("Profile allocated memory")) ADD_PROFILE(MEMORY,       0, _record->durationNs);
                if(ImGui::MenuItem("Profile allocated calls" )) ADD_PROFILE(MEMORY_CALLS, 0, _record->durationNs);
            }
            else {
                if(ImGui::BeginMenu("Profile allocated memory"))  {
                    if(ImGui::MenuItem("Full thread"   )) ADD_PROFILE(MEMORY, 0, _record->durationNs);
                    if(ImGui::MenuItem("Visible region")) ADD_PROFILE(MEMORY, trb.getStartTimeNs(), trb.getTimeRangeNs());
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("Profile allocated calls"))  {
                    if(ImGui::MenuItem("Full thread"   )) ADD_PROFILE(MEMORY_CALLS, 0, _record->durationNs);
                    if(ImGui::MenuItem("Visible region")) ADD_PROFILE(MEMORY_CALLS, trb.getStartTimeNs(), trb.getTimeRangeNs());
                    ImGui::EndMenu();
                }
            }
        }
        ImGui::Separator();

        // Thread color menu
        std::function<void(int)> threadSetColor = [tId, this] (int colorIdx) { getConfig().setThreadColorIdx(tId, colorIdx); };
        displayColorSelectMenu("Thread color", getConfig().getThreadColorIdx(tId), threadSetColor);

        // Log level menu
        if(ImGui::BeginMenu("Level log")) {
            if(ImGui::RadioButton("Debug", &trb.logLevel, 0)) { trb.isCacheDirty = true; ImGui::CloseCurrentPopup(); }
            if(ImGui::RadioButton("Info",  &trb.logLevel, 1)) { trb.isCacheDirty = true; ImGui::CloseCurrentPopup(); }
            if(ImGui::RadioButton("Warn",  &trb.logLevel, 2)) { trb.isCacheDirty = true; ImGui::CloseCurrentPopup(); }
            if(ImGui::RadioButton("Error", &trb.logLevel, 3)) { trb.isCacheDirty = true; ImGui::CloseCurrentPopup(); }
            ImGui::EndMenu();
        }

        ImGui::Separator();
    } // End of menu part specific to threads

    if(ImGui::MenuItem("Expand all threads"))   {
        getConfig().setAllExpanded(true);
        synchronizeThreadLayout();
        ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Collapse all threads")) {
        getConfig().setAllExpanded(false);
        synchronizeThreadLayout();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup(); // End of "Thread menu"
    ImGui::PopID();
    ImGui::PopID();
}
