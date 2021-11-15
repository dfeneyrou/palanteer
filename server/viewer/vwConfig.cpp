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

// This file implements the global and the per-application persistent configurations

// System
#include <cinttypes>

// Internal
#include "bsOs.h"
#include "bsTime.h"
#include "cmRecord.h"
#include "vwConfig.h"
#include "vwMain.h"


#ifndef PL_GROUP_CFG
#define PL_GROUP_CFG 0
#endif


vwConfig::vwConfig(vwMain* main, const bsString& programDataPath) :
    _main(main),
    _programDataPath(programDataPath),
    _configPath(programDataPath+PL_DIR_SEP "configs" PL_DIR_SEP)
{
    plgScope(CFG, "vwConfig::vwConfig");

    // Palette is computed from a selection of 8 well separated (=for the eyes) hues
    // Each hue provides 4 colors: bright saturated, bright pastel, dim saturated, dim pastel
    constexpr float hues[8] = { 40., 60., 96., 175., 210., 240., 280., 310. }; // In degrees
    for(int i=0; i<32; ++i) {
        int i8 = i%8, i16 = i%16;
        float r, g, b;

        // Create the color from the hue modulo 8. Some adjustment are required due to perceptual
        float h = hues[i8]/360.;
        float s = ((i& 0x8)==0)? 1.0 : 0.5;
        float v = ((i&0x10)==0)? 1.0 : 0.55;
        if(i<16 && (i==8 || i8==1 || i8==2 || i8==3)) v -= 0.2;  // Yellow, green, cyan too bright
        else if(i16==5 || i16==6 || i16==7) s -= 0.1;  // Dark blue, violet and magenta  are too saturated

        // Build the dark and light colors from the average one
        ImGui::ColorConvertHSVtoRGB(h, s, bsMin(1.0f, 1.2*v), r, g, b); // Boost a bit the value for light color
        _colorPaletteLight.push_back(ImVec4(r, g, b, 1.));
        ImGui::ColorConvertHSVtoRGB(h, s, 0.9*v, r, g, b); // Reduce a bit the value for dark color
        _colorPaletteDark.push_back(ImVec4(r, g, b, 1.));
    }

    // Optimize allocations
    _order .reserve(vwConst::QUANTITY_THREADID);
    _export.reserve(vwConst::QUANTITY_THREADID);

    // Other
    _lastFileImportPath = osGetCurrentPath();

    // Load global settings
    loadGlobal();
}


vwConfig::~vwConfig(void)
{
    plgScope(CFG, "vwConfig::~vwConfig");
    saveGlobal();
}


// Global configuration
// ====================

bool
vwConfig::setFontSize(int fontSize)
{
    plgScope(CFG, "setFontSize");
    if(_fontSize==fontSize) return true;
    if(fontSize<vwConst::FONT_SIZE_MIN || fontSize>vwConst::FONT_SIZE_MAX) return false;
    plgVar(CFG, fontSize);
    _fontSize          = fontSize;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setCacheMBytes(int cacheMBytes)
{
    plgScope(CFG, "setCacheMBytes");
    if(_cacheMBytes==cacheMBytes) return true;
    if(cacheMBytes<vwConst::CACHE_MB_MIN || cacheMBytes>vwConst::CACHE_MB_MAX) return false;
    plgVar(CFG, cacheMBytes);
    _cacheMBytes       = cacheMBytes;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setHWheelInversion(bool state)
{
    plgScope(CFG, "setHWheelInversion");
    if(_hWheelInversion==(int)state) return true;
    plgVar(CFG, state);
    _hWheelInversion   = (int)state;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setVWheelInversion(bool state)
{
    plgScope(CFG, "setVWheelInversion");
    if(_vWheelInversion==(int)state) return true;
    plgVar(CFG, state);
    _vWheelInversion   = (int)state;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setTimelineVSpacing(float spacing)
{
    plgScope(CFG, "setTimelineVSpacing");
    if(_vTimelineSpacing==spacing) return true;
    plgVar(CFG, spacing);
    _vTimelineSpacing = spacing;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setWindowCatalogVisibility(bool state)
{
    plgScope(CFG, "setWindowCatalogVisibility");
    if(_winVisiCatalog==(int)state) return true;
    plgVar(CFG, state);
    _winVisiCatalog    = (int)state;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setWindowRecordVisibility(bool state)
{
    plgScope(CFG, "setWindowRecordVisibility");
    if(_winVisiRecord==(int)state) return true;
    plgVar(CFG, state);
    _winVisiRecord       = (int)state;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setWindowSearchVisibility(bool state)
{
    plgScope(CFG, "setWindowSearchVisibility");
    if(_winVisiSearch==(int)state) return true;
    plgVar(CFG, state);
    _winVisiSearch     = (int)state;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setWindowConsoleVisibility(bool state)
{
    plgScope(CFG, "setWindowConsoleVisibility");
    if(_winVisiConsole==(int)state) return true;
    plgVar(CFG, state);
    _winVisiConsole    = (int)state;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setWindowSettingsVisibility(bool state)
{
    plgScope(CFG, "setWindowSettingsVisibility");
    if(_winVisiSettings==(int)state) return true;
    plgVar(CFG, state);
    _winVisiSettings   = (int)state;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setStreamConfig(bool isMultiStream, const bsString& multiStreamAppName)
{
    plgScope(CFG, "setStreamConfig");
    if(_multiStreamIsMulti==(int)isMultiStream && _multiStreamAppName==multiStreamAppName) return true;
    plgVar(CFG, isMultiStream);
    _multiStreamIsMulti = (int)isMultiStream;
    _multiStreamAppName = multiStreamAppName;
    _globalNeedsSaving = true;
    return true;
}


bool
vwConfig::setFreezePointEnabled(bool state)
{
    plgScope(CFG, "setFreezePointEnabled");
    if(_freezePointEnabled==(int)state) return true;
    plgVar(CFG, state);
    _freezePointEnabled = (int)state;
    _globalNeedsSaving  = true;
    return true;
}


bool
vwConfig::setPauseStoringState(bool state)
{
    plgScope(CFG, "setPauseStoringState");
    if(_pauseStoringEnabled==(int)state) return true;
    plgVar(CFG, state);
    _pauseStoringEnabled = (int)state;
    _globalNeedsSaving   = true;
    return true;
}


bool
vwConfig::setRecordStoragePath(const bsString& path)
{
    plgScope(CFG, "setRecordStoragePath");
    if(_recordStoragePath==path) return true;
    _recordStoragePath = path;
    plgData(CFG, "path", path.toChar());
    _globalNeedsSaving  = true;
    return true;
}


bool
vwConfig::setLastFileImportPath(const bsString& path)
{
    plgScope(CFG, "setLastFileImportPath");
    if(_lastFileImportPath==path) return true;
    _lastFileImportPath = path;
    plgData(CFG, "path", path.toChar());
    _globalNeedsSaving  = true;
    return true;
}


bool
vwConfig::setLastLoadedRecordPath(const bsString& path)
{
    plgScope(CFG, "setLastLoadedRecordPath");
    if(_lastLoadedRecordPath==path) return true;
    _lastLoadedRecordPath = path;
    plgData(CFG, "path", path.toChar());
    _globalNeedsSaving  = true;
    return true;
}


bool
vwConfig::setLastFileExtStringsPath(const bsString& path)
{
    plgScope(CFG, "setLastFileExtStringsPath");
    if(_lastFileExtStringsPath==path) return true;
    _lastFileExtStringsPath = path;
    plgData(CFG, "path", path.toChar());
    _globalNeedsSaving  = true;
    return true;
}


void
vwConfig::getKeepOnlyLastNRecord(const bsString& appName, bool& state, int& n)
{
    state = false;
    n     = 10;
    for(const KeepAppRecordParam& k : _keepOnlyLastRecord) {
        if(k.name!=appName) continue;
        state = k.state;
        n     = k.recordQty;
        return;
    }
}


void
vwConfig::setKeepOnlyLastNRecord(const bsString& appName, bool state, int n)
{
    plgScope(CFG, "setKeepOnlyLastNRecord");
    // Find the entry
    for(KeepAppRecordParam& k : _keepOnlyLastRecord) {
        if(k.name!=appName) continue;
        if((!!k.state)==state && k.recordQty==n) return; // No change
        k.state     = state;
        k.recordQty = n;
        _globalNeedsSaving  = true;
        return;
    }
    // Entry not found: add it
    _keepOnlyLastRecord.push_back({appName, state, n});
    plgVar(CFG, appName.toChar(), state, n);
    _globalNeedsSaving  = true;
}


void
vwConfig::getExtStringsPath(const bsString& appName, bsString& path)
{
    path.clear();
    for(const AppExtStringsPath& k : _appExtStringsPath) {
        if(k.name!=appName) continue;
        path = k.path;
        return;
    }
}


void
vwConfig::setExtStringsPath(const bsString& appName, const bsString& path)
{
    plgScope(CFG, "setKeepOnlyLastNRecord");
    // Find the entry
    for(AppExtStringsPath& k : _appExtStringsPath) {
        if(k.name!=appName) continue;
        if(k.path==path) return; // No change
        k.path = path;
        _globalNeedsSaving  = true;
        return;
    }
    // Entry not found: add it
    _appExtStringsPath.push_back({appName, path});
    plgVar(CFG, appName.toChar());
    _globalNeedsSaving  = true;
}



// Application-specific configuration
// ==================================

void
vwConfig::notifyNewRecord(cmRecord* record)
{
    plgScope(CFG, "vwConfig::notifyNewRecord");

    // Canonical initialization from the record
    // ========================================
    _groups.clear();
    _order.clear();
    _elems.clear();
    _liveConfigThreads.clear();
    _liveConfigGroups.clear();
    _liveConfigElems.clear();
    _cliHistory.clear();
    _currentLayout = { "", "", {} };
    _templateLayouts.clear();
    memset(&_threads[0], 0, sizeof(_threads));

    // Threads
    for(int threadId=0; threadId<record->threads.size(); ++threadId) {

        // Initial color depends on the thread name characters
        int groupNameIdx = record->threads[threadId].groupNameIdx;
        const cmRecord::String& s = record->getString(record->threads[threadId].nameIdx);

        // Store thread & group with canonical order
        _threads[threadId] = { threadId, groupNameIdx, (int)(s.hash%_colorPaletteDark.size()), true, s.hash };
        _order.push_back(threadId);
        if(groupNameIdx>=0) {
            bool isPresent = false;
            for(const Group& g : _groups) if(g.nameIdx==groupNameIdx) isPresent = true;
            if(!isPresent) _groups.push_back( { groupNameIdx, true, record->getString(groupNameIdx).hash } );
        }
    }

    if(!record->locks.empty()) {
        _threads[vwConst::LOCKS_THREADID] = { vwConst::LOCKS_THREADID, -1, 0, true, vwConst::LOCKS_THREADID }; // Hash equal to fixed ID
        int tmp = vwConst::LOCKS_THREADID; // Why tmp? Because constexpr in C++11 (fixed in C++17) is linked if odr-used...
        _order.push_back(tmp);
    }
    if(record->coreQty>0) {
        _threads[vwConst::CORE_USAGE_THREADID] = { vwConst::CORE_USAGE_THREADID, -1, 0, true, vwConst::CORE_USAGE_THREADID }; // Hash equal to fixed ID
        int tmp = vwConst::CORE_USAGE_THREADID; // Why tmp? Because constexpr in C++11 (fixed in C++17) is linked if odr-used...
        _order.push_back(tmp);
    }

    // Curve colors
    _elems.resize(record->elems.size());
    for(int i=0; i<_elems.size(); ++i) {
        u64 hash = record->elems[i].hashPath; // Depends only on path made from name strings.
        _elems[i].colorIdx  = hash%_colorPaletteDark.size();
        _elems[i].pointSize = 3;
        _elems[i].style     = LINE;
        _elems[i].hash      = hash;
        int eType = (record->elems[i].flags&PL_FLAG_TYPE_MASK);
        if(eType==PL_FLAG_TYPE_DATA_STRING || eType==PL_FLAG_TYPE_MARKER) _elems[i].style = STEP;
        if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) { _elems[i].style = POINT; _elems[i].pointSize = 6; }
    }

    // Load the application config file if exists
    loadApplication(record->appName);
}


void
vwConfig::notifyUpdatedRecord(cmRecord* record)
{
    // New (user) threads & groups
    for(int threadId=0; threadId<record->threads.size(); ++threadId) {
        const cmRecord::String& s = record->getString(record->threads[threadId].nameIdx);
        if(_threads[threadId].hash==s.hash) continue; // No change. Indeed, it may change due to live thread name update

        // Threads
        int groupNameIdx = record->threads[threadId].groupNameIdx;
        _threads[threadId] = { threadId, groupNameIdx, (int)(s.hash%_colorPaletteDark.size()), true, s.hash };

        // Configuration
        for(ThreadLayout& tCfg : _liveConfigThreads) {
            if(tCfg.hash!=s.hash) continue;
            _threads[threadId].colorIdx   = tCfg.colorIdx;
            _threads[threadId].isExpanded = tCfg.isExpanded;
            break;
        }

        // Groups
        if(groupNameIdx>=0) {
            bool isPresent = false;
            for(const Group& g : _groups) if(g.nameIdx==groupNameIdx) isPresent = true;
            if(!isPresent) {
                bool foundInConfig = false;
                const cmRecord::String& gs = record->getString(groupNameIdx);
                // Use the values from the config file, if present
                for(int i=0; i<_liveConfigGroups.size(); ++i) {
                    Group& cfgGroup = _liveConfigGroups[i];
                    if(gs.hash!=cfgGroup.hash) continue;
                    _groups.push_back( { groupNameIdx, cfgGroup.isExpanded, record->getString(groupNameIdx).hash } );
                    cfgGroup = _liveConfigGroups.back(); _liveConfigGroups.pop_back();
                    foundInConfig = true;
                    break;
                }
                // Else default values
                if(!foundInConfig) {
                    _groups.push_back( { groupNameIdx, true, record->getString(groupNameIdx).hash } );
                }
            }
        }
    }

    if(!record->locks.empty() && _threads[vwConst::LOCKS_THREADID].hash==0) {
        _threads[vwConst::LOCKS_THREADID] = { vwConst::LOCKS_THREADID, -1, 0, true, vwConst::LOCKS_THREADID }; // Hash equal to fixed ID
        for(ThreadLayout& tCfg : _liveConfigThreads) {
            if(tCfg.hash!=vwConst::LOCKS_THREADID) continue;
            _threads[vwConst::LOCKS_THREADID].colorIdx   = tCfg.colorIdx;
            _threads[vwConst::LOCKS_THREADID].isExpanded = tCfg.isExpanded;
            break;
        }
    }

    if(record->coreQty>0 && _threads[vwConst::CORE_USAGE_THREADID].hash==0) {
        _threads[vwConst::CORE_USAGE_THREADID] = { vwConst::CORE_USAGE_THREADID, -1, 0, true, vwConst::CORE_USAGE_THREADID }; // Hash equal to fixed ID
        for(ThreadLayout& tCfg : _liveConfigThreads) {
            if(tCfg.hash!=vwConst::CORE_USAGE_THREADID) continue;
            _threads[vwConst::CORE_USAGE_THREADID].colorIdx   = tCfg.colorIdx;
            _threads[vwConst::CORE_USAGE_THREADID].isExpanded = tCfg.isExpanded;
            break;
        }
    }

    // Configure new elements
    _elems.reserve(record->elems.size());
    while(_elems.size()<record->elems.size()) {
        cmRecord::Elem& recordElem = record->elems[_elems.size()];
        u64  hashPath = recordElem.hashPath;
        if(recordElem.isThreadHashed) hashPath = bsHashStep(record->threads[recordElem.threadId].threadUniqueHash, hashPath);
        bool foundInConfig = false;
        // Use the values from the config file, if present
        for(int i=0; i<_liveConfigElems.size(); ++i) {
            Elem& cfgElem = _liveConfigElems[i];
            if(hashPath!=cfgElem.hash) continue;
            _elems.push_back(cfgElem);
            cfgElem = _liveConfigElems.back(); _liveConfigElems.pop_back();
            foundInConfig = true;
            break;
        }
        // Else default values
        if(!foundInConfig) {
            int eType = (record->elems[_elems.size()].flags&PL_FLAG_TYPE_MASK);
            CurveStyle style = LINE;
            int        pointSize = 3;
            if(eType==PL_FLAG_TYPE_DATA_STRING || eType==PL_FLAG_TYPE_MARKER) style = STEP;
            if(eType==PL_FLAG_TYPE_LOCK_NOTIFIED) { style = LOLLIPOP; pointSize = 4; }
            _elems.push_back({(int)(hashPath%_colorPaletteDark.size()), pointSize, style, hashPath});
        }
    }

    reorderThreadLayout();
    precomputeThreadExport();
}


void
vwConfig::precomputeThreadExport(void)
{
    _export.clear();
    for(int idx : _order) _export.push_back(_threads[idx]);
    _appliNeedsSaving  = true;
}


void
vwConfig::randomizeThreadColors(void)
{
    plgScope(CFG, "randomizeThreadColors");
    u64 kindOfRand = bsHashStep(bsGetClockUs());
    for(int i=0; i<_order.size(); ++i) {
        _threads[_order[i]].colorIdx = (kindOfRand>>8)%_colorPaletteDark.size();
        kindOfRand = bsHashStep(kindOfRand);
    }
    precomputeThreadExport();
}


void
vwConfig::randomizeCurveColors(void)
{
    plgScope(CFG, "randomizeCurveColors");
    u64 kindOfRand = bsHashStep(bsGetClockUs());
    for(Elem& elem : _elems) {
        elem.colorIdx = (kindOfRand>>8)%_colorPaletteDark.size();
        kindOfRand    = bsHashStep(kindOfRand);
    }
    precomputeThreadExport();
}


void
vwConfig::setThreadColorIdx(int threadId, int colorIdx)
{
    if(_threads[threadId].colorIdx!=colorIdx) {
        plgScope(CFG, "setThreadColorIdx");
        plgVar(CFG, threadId, colorIdx);
        _threads[threadId].colorIdx = colorIdx;
        precomputeThreadExport();
    }
}


void
vwConfig::setThreadExpanded(int threadId, bool isExpanded)
{
    if(_threads[threadId].isExpanded!=isExpanded) {
        plgScope(CFG, "setThreadExpanded");
        plgVar(CFG, threadId, isExpanded);
        _threads[threadId].isExpanded = isExpanded;
        precomputeThreadExport();
   }
}


void
vwConfig::setGroupExpanded(int groupNameIdx, bool isExpanded)
{
    plgScope(CFG, "setGroupExpanded");
    for(Group& g : _groups) {
        if(g.nameIdx==groupNameIdx) {
            g.isExpanded = isExpanded; _appliNeedsSaving = true; break;
        }
    }
}


void
vwConfig::moveDragThreadId(bool srcIsGroup, int srcThreadId, int dstThreadId)
{
    plgScope(CFG,  "moveDragThreadId");
    plgVar(CFG,  srcIsGroup, srcThreadId, dstThreadId);

    // Find the order index of both threads
    int iSrc = -1, iDst = -1;
    for(int i=0; i<_order.size(); ++i) {
        if(_threads[_order[i]].threadId==srcThreadId) iSrc = i;
        if(_threads[_order[i]].threadId==dstThreadId) iDst = i;
    }
    if(iSrc<0 || iDst<0 || iSrc==iDst) return;

    int srcGroupNameIdx = _threads[_order[iSrc]].groupNameIdx;
    int dstGroupNameIdx = _threads[_order[iDst]].groupNameIdx;
    int moveQty = 1;

    if(srcIsGroup) {
        // Get the group start and size
        while(iSrc<0 && _threads[_order[iSrc-1]].groupNameIdx==srcGroupNameIdx) --iSrc; // Go to source group start
        int iSrc2 = iSrc+1;
        while(iSrc2<_order.size() && _threads[_order[iSrc2]].groupNameIdx==srcGroupNameIdx) ++iSrc2;
        moveQty = iSrc2-iSrc;

        // Ensure destination is a start of a group
        if(dstGroupNameIdx>=0) {
            if     (iSrc<iDst) while(iDst+1<_order.size() && _threads[_order[iDst+1]].groupNameIdx==dstGroupNameIdx) ++iDst; // Go to group end
            else if(iSrc>iDst) while(iDst>0 && _threads[_order[iDst-1]].groupNameIdx==dstGroupNameIdx) --iDst;               // Go to group start
        }
    }

    else if(srcGroupNameIdx!=dstGroupNameIdx) { // In the other case (equal), thread move in intra or extra group  are directly ok
        // Source is not part of a group (which means dest is part of a group)
        if(srcGroupNameIdx<0) {
            if     (iSrc<iDst) while(iDst+1<_order.size() && _threads[_order[iDst+1]].groupNameIdx==dstGroupNameIdx) ++iDst; // Go to group end
            else if(iSrc>iDst) while(iDst>0 && _threads[_order[iDst-1]].groupNameIdx==dstGroupNameIdx) --iDst;               // Go to group start
        }
        // Source is part of a group, so dest shall be confined in that source group
        else {
            if     (iSrc<iDst) { iDst = iSrc; while(iDst+1<_order.size() && _threads[_order[iDst+1]].groupNameIdx==srcGroupNameIdx) ++iDst; }
            else if(iSrc>iDst) { iDst = iSrc; while(iDst>0 && _threads[_order[iDst-1]].groupNameIdx==srcGroupNameIdx) --iDst; }
        }
    }

    // Move the src thread(s), drag'n drop way
    int* savedIdx = (int*)alloca(moveQty*sizeof(int));
    for(int i=0; i<moveQty; ++i) savedIdx[i] = _order[iSrc+i];
    if(iSrc<iDst) {
        while(iSrc<=iDst-moveQty) { _order[iSrc] = _order[iSrc+moveQty]; ++iSrc; }
        for(int i=0; i<moveQty; ++i) _order[iDst-moveQty+1+i] = savedIdx[i];
    }
    else {
        while(iSrc>iDst) { _order[iSrc+moveQty-1] = _order[iSrc-1]; --iSrc; }
        for(int i=0; i<moveQty; ++i) _order[iDst+i] = savedIdx[i];
    }

    precomputeThreadExport();
}


bool
vwConfig::setLockLatencyUs(int lockLatencyUs)
{
    plgScope(CFG, "setLockLatencyUs");
    if(_lockLatencyUs==lockLatencyUs) return true;
    if(lockLatencyUs<0 || lockLatencyUs>vwConst::LOCK_LATENCY_LIMIT_MAX_US) return false;
    plgVar(CFG, lockLatencyUs);
    _lockLatencyUs     = lockLatencyUs;
    _appliNeedsSaving  = true;
    return true;
}


// Load & save
// ============

bool
vwConfig::saveGlobal(void)
{
    if(!_globalNeedsSaving) return true;
    plgScope(CFG, "saveGlobal");

    FILE* fh = fopen((_configPath+"palanteer.cfg").toChar(), "w");
    if(!fh) return false;
    _globalNeedsSaving = false;

    fprintf(fh, "fontSize %d\n",    _fontSize);
    fprintf(fh, "cacheMBytes %d\n", _cacheMBytes);
    fprintf(fh, "hWheelInversion %d\n", _hWheelInversion);
    fprintf(fh, "vWheelInversion %d\n", _vWheelInversion);
    fprintf(fh, "vTimelineSpacing %d\n", (int)(100.*_vTimelineSpacing));
    fprintf(fh, "winVisiCatalog %d\n", _winVisiCatalog);
    fprintf(fh, "winVisiRecord %d\n",    _winVisiRecord);
    fprintf(fh, "winVisiSearch %d\n",    _winVisiSearch);
    fprintf(fh, "winVisiConsole %d\n",    _winVisiConsole);
    fprintf(fh, "winVisiSettings %d\n",    _winVisiSettings);
    fprintf(fh, "multiStreamIsMulti %d\n", _multiStreamIsMulti);
    fprintf(fh, "multiStreamAppName %s\n", _multiStreamAppName.toChar());
    fprintf(fh, "freezePointEnabled %d\n", _freezePointEnabled);
    fprintf(fh, "pauseStoringEnabled %d\n", _pauseStoringEnabled);
    fprintf(fh, "recordStoragePath %s\n", _recordStoragePath.toChar());
    fprintf(fh, "lastFileImportPath %s\n", _lastFileImportPath.toChar());
    fprintf(fh, "lastLoadedRecordPath %s\n", _lastLoadedRecordPath.toChar());
    fprintf(fh, "lastFileExtStringsPath %s\n", _lastFileExtStringsPath.toChar());

    for(const KeepAppRecordParam& k : _keepOnlyLastRecord) {
        fprintf(fh, "keepOnlyLastRecord %d %d %s\n", k.state, k.recordQty, k.name.toChar());
    }
    for(const AppExtStringsPath& k : _appExtStringsPath) {
        fprintf(fh, "appExtStringsPath %s|%s\n", k.path.toChar(), k.name.toChar()); // '|' is the separator between the strings (which can contain spaces)
    }

    fclose(fh);
    return true;
}


void
vwConfig::loadGlobal(void)
{
    plgScope(CFG, "loadGlobal");
    char tmpStr[512];
    char tmpStr2[512];
    constexpr int lineSize = 1024;
    char line[lineSize];
    FILE* fh = fopen((_configPath+"palanteer.cfg").toChar(), "r");
    if(!fh) {
        _main->log(cmLogKind::LOG_ERROR, "Unable to open the global configuration %s\n", (_configPath+"palanteer.cfg").toChar());
        _recordStoragePath = _programDataPath+PL_DIR_SEP "records" PL_DIR_SEP;
        _globalNeedsSaving = true;
        return;
    }
    _globalNeedsSaving = false;
    int tmp;

    while(fgets(line, lineSize, fh)) {
        // Get keyword
        int kwLength = 0;
        while(kwLength<lineSize && line[kwLength] && line[kwLength]!=' ') ++kwLength;

        // Parse the data
#define READ_GLOBAL(fieldName, readQty, ...)                            \
        if(!strncmp(line, #fieldName, kwLength)) {                      \
            if(sscanf(line+kwLength+1, __VA_ARGS__)!=readQty)           \
                { _main->log(cmLogKind::LOG_WARNING, "Unable to read global config for field '" #fieldName "'\n"); } \
        }

        READ_GLOBAL(fontSize,    1, "%d", &_fontSize);
        READ_GLOBAL(cacheMBytes, 1, "%d", &_cacheMBytes);
        READ_GLOBAL(hWheelInversion, 1, "%d", &_hWheelInversion);
        READ_GLOBAL(vWheelInversion, 1, "%d", &_vWheelInversion);
        READ_GLOBAL(vTimelineSpacing, 1, "%d", &tmp);
        if(!strncmp(line, "vTimelineSpacing", kwLength)) {
            if(sscanf(line+kwLength+1, "%d", &tmp)!=1) { _main->log(cmLogKind::LOG_WARNING, "Unable to read global config for field 'vTimelineSpacing'\n"); }
            _vTimelineSpacing = 0.01*tmp;
        }
        READ_GLOBAL(winVisiCatalog, 1, "%d", &_winVisiCatalog);
        READ_GLOBAL(winVisiRecord, 1, "%d", &_winVisiRecord);
        READ_GLOBAL(winVisiSearch, 1, "%d", &_winVisiSearch);
        READ_GLOBAL(winVisiConsole, 1, "%d", &_winVisiConsole);
        READ_GLOBAL(winVisiSettings, 1, "%d", &_winVisiSettings);
        READ_GLOBAL(multiStreamIsMulti, 1, "%d", &_multiStreamIsMulti);
        if(!strncmp(line, "multiStreamAppName", kwLength)) {
            _multiStreamAppName = bsString(&line[kwLength]).strip();
            if(!_multiStreamAppName.empty() && _multiStreamAppName.back()=='\n') _multiStreamAppName.pop_back();
        }
        READ_GLOBAL(freezePointEnabled, 1, "%d", &_freezePointEnabled);
        READ_GLOBAL(pauseStoringEnabled, 1, "%d", &_pauseStoringEnabled);

        if(!strncmp(line, "lastFileImportPath", kwLength)) {
            _lastFileImportPath = bsString(&line[kwLength]).strip();
            if(!_lastFileImportPath.empty() && _lastFileImportPath.back()=='\n') _lastFileImportPath.pop_back();
        }

        if(!strncmp(line, "recordStoragePath", kwLength)) {
            _recordStoragePath = bsString(&line[kwLength]).strip();
            if(!_recordStoragePath.empty() && _recordStoragePath.back()=='\n') _recordStoragePath.pop_back();
        }

        if(!strncmp(line, "lastLoadedRecordPath", kwLength)) {
            _lastLoadedRecordPath = bsString(&line[kwLength]).strip();
            if(!_lastLoadedRecordPath.empty() && _lastLoadedRecordPath.back()=='\n') _lastLoadedRecordPath.pop_back();
        }

        if(!strncmp(line, "lastFileExtStringsPath", kwLength)) {
            _lastFileExtStringsPath = bsString(&line[kwLength]).strip();
            if(!_lastFileExtStringsPath.empty() && _lastFileExtStringsPath.back()=='\n') _lastFileExtStringsPath.pop_back();
        }

        if(!strncmp(line, "keepOnlyLastRecord", kwLength)) {
            int state, n;
            if(sscanf(line+kwLength+1, "%d %d %255[^\n]", &state, &n, tmpStr)!=3)
                { _main->log(cmLogKind::LOG_WARNING, "Unable to read global config for field 'keepOnlyLastRecord'\n"); }
            else {
                _keepOnlyLastRecord.push_back({ tmpStr, state, n });
            }
        }

        if(!strncmp(line, "appExtStringsPath", kwLength)) {
            if(sscanf(line+kwLength+1, "%255[^|\n]|%255[^\n]", tmpStr2, tmpStr)!=2)
                { _main->log(cmLogKind::LOG_WARNING, "Unable to read global config for field 'appExtStringsPath'\n"); }
            else {
                _appExtStringsPath.push_back({ tmpStr, tmpStr2 });
            }
        }

    } // End of loop on lines

    // Sanity
    if(_recordStoragePath.empty()) {
        _recordStoragePath = _programDataPath+PL_DIR_SEP "records" PL_DIR_SEP;
        _globalNeedsSaving = true;
    }
    if(_recordStoragePath.back()!=PL_DIR_SEP_CHAR) {
        _recordStoragePath.push_back(PL_DIR_SEP_CHAR);
        _globalNeedsSaving = true;
    }

    fclose(fh);
}


bool
vwConfig::saveApplication(const bsString& appName)
{
    FILE* fh = fopen((_configPath+bsString("app_")+appName+".cfg").toChar(), "w");
    if(!fh) return false;
    _appliNeedsSaving  = false;

    // Lock latency in µs
    fprintf(fh, "locklatencyus %d\n", _lockLatencyUs);

    // Thread layout
    for(int i=0; i<_order.size(); ++i) {
        const ThreadLayout& t = _threads[_order[i]];
        fprintf(fh, "thread %" PRIX64 " %d %d\n", t.hash, t.colorIdx, (int)t.isExpanded);
    }

    // Group layout
    for(const Group& g : _groups) {
        fprintf(fh, "group %" PRIX64 " %d\n", g.hash, (int)g.isExpanded);
    }

    // Elem colors
    for(const Elem& elem : _elems) {
        fprintf(fh, "elem %" PRIX64 " %d %d %d\n", elem.hash, elem.colorIdx, elem.pointSize, (int)elem.style);
    }

    // CLI history
    for(const bsString& s : _cliHistory) {
        fprintf(fh, "clihistory %s\n", s.toChar());
    }

    // Workspace
    if(_currentLayout.windows.size()) {
        bsString w = _currentLayout.windows;
        for(int i=0; i<w.size(); ++i) if(w[i]=='\n') w[i] = 0x1F; // Keep one line for the full bloc...
        fprintf(fh, "screenlayout %s\n", w.toChar());
        for(const LayoutView& view : _currentLayout.views) {
            fprintf(fh, "screenview %d %s\n", view.id, view.descr.toChar());
        }
    }

    // Template layouts
    for(int tmplIdx=0; tmplIdx<_templateLayouts.size(); ++tmplIdx) {
        const ScreenLayout& t = _templateLayouts[tmplIdx];
        bsString w = t.windows;
        for(int i=0; i<w.size(); ++i) if(w[i]=='\n') w[i] = 0x1F; // Keep one line for the full bloc...
        fprintf(fh, "templatename %s\n", t.name.toChar());
        fprintf(fh, "templatelayout %s\n", w.toChar());
        for(const LayoutView& view : t.views) {
            fprintf(fh, "templateview %d %s\n", view.id, view.descr.toChar());
        }
    }

    // Extra lines
    for(const bsString& s : _extraLines) {
        fprintf(fh, "%s", s.toChar());
    }

    fclose(fh);
    return true;
}


void
vwConfig::loadApplication(const bsString& appName)
{
    plgScope(CFG, "loadApplication");
    plgData(CFG, "name", appName.toChar());

    FILE* fh = fopen((_configPath+bsString("app_")+appName+".cfg").toChar(), "r");
    if(!fh) {
        reorderThreadLayout();
        precomputeThreadExport();
        return;
    }

    _extraLines.clear();
    _appliNeedsSaving  = false;

    constexpr int lineSize = 100*1024; // 100 KB limit for the ImGui Layout
    bsString lineBuffer; lineBuffer.resize(lineSize);
    char* line = (char*)&lineBuffer[0];

    // Loop on config file lines
    while(fgets(line, lineSize, fh)) {
        plAssert(strlen(line)<lineSize-1, "ImGui layout configuration is too large for this buffer...");

        // Get keyword
        int kwLength = 0;
        while(kwLength<lineSize && line[kwLength] && line[kwLength]!=' ') ++kwLength;

        // Parse the data
#define READ_APPLI(fieldName, readQty, ...)                             \
        isKwFound = false;                                                \
        if(kwLength==strlen(#fieldName) && !strncmp(line, #fieldName, kwLength)) { \
            if(sscanf(line+kwLength+1, __VA_ARGS__)!=readQty)           \
                { _main->log(cmLogKind::LOG_WARNING, "Unable to read application config for field '" #fieldName "'\n"); } \
            else isKwFound = true;                                        \
        }
        u64 hash = 0;
        int tmp1, tmp2, tmp3;
        bool isKwFound = false;

        // Lock latency
        READ_APPLI(locklatencyus, 1,"%d", &tmp1);
        if(isKwFound) _lockLatencyUs = tmp1;

        // "thread" color, visibility. And implicitely the order
        READ_APPLI(thread, 3, "%" PRIX64 " %d %d", &hash, &tmp1, &tmp2);
        if(isKwFound) {
            bool isFound = false;
            for(int tId=0; !isFound && tId<vwConst::QUANTITY_THREADID; ++tId) {
                ThreadLayout& tl = _threads[tId];
                if(hash!=tl.hash) continue;
                tl.colorIdx  = tmp1; tl.isExpanded = tmp2; isFound = true; break;
            }
            if(!isFound && _extraLines.size()<vwConst::MAX_EXTRA_LINE_PER_CONFIG) _extraLines.push_back(line);
            _liveConfigThreads.push_back({-1, -1, tmp1, (bool)tmp2, hash});  // Save hash-> (order + config). Finalized in reorderThreadLayout
        }

        // "group" group visibility
        READ_APPLI(group, 2, "%" PRIX64 " %d", &hash, &tmp1);
        if(isKwFound) {
            bool isFound = false;
            for(Group& g : _groups) {
                if(hash==g.hash) { g.isExpanded = tmp1; isFound = true; break; }
            }
            if(!isFound) {
                if(_extraLines.size()<vwConst::MAX_EXTRA_LINE_PER_CONFIG) _extraLines.push_back(line);
            }
            _liveConfigGroups.push_back({-1, (bool)tmp1, hash});
        }

        // "elem" display attribute of each curve
        READ_APPLI(elem, 4, "%" PRIX64 " %d %d %d", &hash, &tmp1, &tmp2, &tmp3);
        if(isKwFound) {
            bool isFound = false;
            // @#OPTIM Hash table useful here as there can be many elem?
            for(Elem& elem : _elems) {
                if(hash!=elem.hash) continue;
                elem.colorIdx = tmp1; elem.pointSize = tmp2; elem.style = (vwConfig::CurveStyle)tmp3;
                isFound = true; break;
            }
            if(!isFound) {
                if(_extraLines.size()<vwConst::MAX_EXTRA_LINE_PER_CONFIG) _extraLines.push_back(line);
                _liveConfigElems.push_back({tmp1, tmp2, (CurveStyle)tmp3, hash});
            }
        }

        // "cli history"
        if(kwLength==strlen("clihistory") && !strncmp(line, "clihistory", kwLength)) {
            _cliHistory.push_back(bsString(line+11, line+strlen(line)-1));
        }

        // Workspace layout
        if(kwLength==strlen("screenlayout") && !strncmp(line, "screenlayout", kwLength)) {
            bsString& w = _currentLayout.windows;
            w = bsString(line+13, line+strlen(line)-1); // Ignore \n
            for(int i=0; i<w.size(); ++i) if(w[i]==0x1F) w[i] = '\n'; // Put back the multi-line info...
            _currentLayout.views.clear();
        }
        READ_APPLI(screenview, 1, "%d", &tmp1);
        if(isKwFound) {
            int offset = 11;
            while(*(line+offset) && *(line+offset)!=' ') ++offset; // Skip the first number
            while(*(line+offset)==' ') ++offset; // Skip the separating space
            _currentLayout.views.push_back({tmp1, bsString(line+offset, line+strlen(line)-1)});
        }

        // Template layout
        if(kwLength==strlen("templatename") && !strncmp(line, "templatename", kwLength)) {
            _templateLayouts.push_back({ bsString(line+13, line+strlen(line)-1), "", {} });
        }
        if(kwLength==strlen("templatelayout") && !strncmp(line, "templatelayout", kwLength)) {
            bsString& w = _templateLayouts.back().windows;
            w = bsString(line+15, line+strlen(line)-1); // Ignore \n
            for(int i=0; i<w.size(); ++i) if(w[i]==0x1F) w[i] = '\n'; // Put back the multi-line info...
        }
        READ_APPLI(templateview, 1, "%d", &tmp1);
        if(isKwFound) {
            int offset = 13;
            while(*(line+offset) && *(line+offset)!=' ') ++offset; // Skip the first number
            while(*(line+offset)==' ') ++offset; // Skip the separating space
            _templateLayouts.back().views.push_back({tmp1, bsString(line+offset, line+strlen(line)-1)});
        }
    } // while(fgets...)

    // Finalize
    fclose(fh);
    reorderThreadLayout();
    precomputeThreadExport();
}


void
vwConfig::reorderThreadLayout(void)
{
    _order.clear();
    bool alreadyInArray[vwConst::QUANTITY_THREADID];
    memset(alreadyInArray, 0, sizeof(alreadyInArray));

    // Loop on ordered thread config and build the "order" array
    for(ThreadLayout& tCfg : _liveConfigThreads) {
        for(int tId=0; tId<vwConst::QUANTITY_THREADID; ++tId) {
            ThreadLayout& tl = _threads[tId];
            if(tCfg.hash!=tl.hash || alreadyInArray[tId]) continue;
            alreadyInArray[tId] = true; // Required for robustness to avoid duplicated thread in layout...

            // Because of potentially order-obsolete extra lines, provided order may not be fully trusted.
            // If group consistency is not verified, the order will be the default one in the next paragraph
            int lastGroupCompatOrderIdx = -1;
            if(tl.groupNameIdx>=0) {
                for(int j=0; j<_order.size(); ++j) {
                    if(_threads[_order[j]].groupNameIdx==tl.groupNameIdx) lastGroupCompatOrderIdx = j;
                }
            }
            if(lastGroupCompatOrderIdx<0 || lastGroupCompatOrderIdx==_order.size()-1) {  // Ensures that the order is valid
                _order.push_back(tId);
            }
            break;
        }
    }

    // Ensure all threads are present in the order array
    for(int tId=0; tId<vwConst::QUANTITY_THREADID; ++tId) {
        ThreadLayout& tl = _threads[tId];
        if(tl.hash==0) continue; // Empty thread slot

        // Check that the thread is present in the order list
        bool isPresent = false;
        int  groupCompatOrderIdx = -1;
        for(int i=0; i<_order.size(); ++i) {
            if(_threads[_order[i]].hash==tl.hash) { isPresent = true; break; }
            else if(_threads[_order[i]].groupNameIdx==tl.groupNameIdx) groupCompatOrderIdx = i;
        }
        if(isPresent) continue;

        // Insert the thread at a group-compatible place
       if(groupCompatOrderIdx<0) {
            _order.push_back(tId);
        } else {
            _order.resize(_order.size()+1);
            if(groupCompatOrderIdx+2<_order.size()) {
                memmove(&_order[groupCompatOrderIdx+2], &_order[groupCompatOrderIdx+1], (_order.size()-2-groupCompatOrderIdx)*sizeof(int));
            }
            _order[groupCompatOrderIdx+1] = tId;
        }
    }

}
