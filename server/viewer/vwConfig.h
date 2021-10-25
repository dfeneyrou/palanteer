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

// External
#include "imgui.h"

// Internal
#include "bsVec.h"
#include "bsString.h"
#include "vwConst.h"

class cmRecord;
class vwMain;

// Passive configuration for persistent storage only.
// This means that the effects of changing a parameter shall be handled on caller side.
class vwConfig {
 public:
    vwConfig(vwMain* main, const bsString& programDataPath);
    ~vwConfig(void);
    const bsString& getConfigPath(void) const { return _configPath; }

    // Load & save
    void loadGlobal(void);
    bool saveGlobal(void);
    void loadApplication(const bsString& appName);
    bool saveApplication(const bsString& appName);

    // Paths

    // Global parameters
    // =================

    // Preferences & interface
    int  getFontSize   (void) const { return _fontSize; }
    bool setFontSize   (int fontSize);
    int  getCacheMBytes(void) const { return _cacheMBytes; }
    bool setCacheMBytes(int cacheMBytes);
    int  getHWheelInversion(void) const { return _hWheelInversion? -1:1; }
    bool setHWheelInversion(bool state);
    int  getVWheelInversion(void) const { return _vWheelInversion? -1:1; }
    bool setVWheelInversion(bool state);
    float getTimelineVSpacing(void) const { return _vTimelineSpacing; } // In fontHeight
    bool  setTimelineVSpacing(float vSpacing);
    bool setWindowCatalogVisibility(bool state);
    bool getWindowCatalogVisibility(void) const { return _winVisiCatalog; }
    bool setWindowRecordVisibility(bool state);
    bool getWindowRecordVisibility(void) const { return _winVisiRecord; }
    bool setWindowSearchVisibility(bool state);
    bool getWindowSearchVisibility(void) const { return _winVisiSearch; }
    bool setWindowConsoleVisibility(bool state);
    bool getWindowConsoleVisibility(void) const { return _winVisiConsole; }
    bool setWindowSettingsVisibility(bool state);
    bool getWindowSettingsVisibility(void) const { return _winVisiSettings; }

    // Live
    bool setFreezePointEnabled(bool state);
    bool getFreezePointEnabled(void) const { return _freezePointEnabled; }
    bool setPauseStoringState(bool state);
    bool getPauseStoringState(void) const { return _pauseStoringEnabled; }

    // Paths
    const bsString& getRecordStoragePath(void) const { return _recordStoragePath; }
    bool            setRecordStoragePath(const bsString& path);
    bsString getLastFileImportPath(void) { return _lastFileImportPath; }
    bool     setLastFileImportPath(const bsString& path);
    bsString getLastLoadedRecordPath(void) { return _lastLoadedRecordPath; }
    bool     setLastLoadedRecordPath(const bsString& path);
    bsString getLastFileExtStringsPath(void) { return _lastFileExtStringsPath; }
    bool     setLastFileExtStringsPath(const bsString& path);

    // Catalog management (global because accessed even when no record is loaded)
    void setKeepOnlyLastNRecord(const bsString& appName, bool  state, int  n);
    void getKeepOnlyLastNRecord(const bsString& appName, bool& state, int& n);
    void setExtStringsPath(const bsString& appName, const bsString& path);
    void getExtStringsPath(const bsString& appName, bsString& path);


    // Application-under-analysis specific parameters
    // ==============================================

    // Thread layout
    struct ThreadLayout {
        int  threadId, groupNameIdx, colorIdx; // groupNameIdx=-1 means no group
        bool isExpanded;
        u64  hash;
    };
    void setThreadExpanded(int threadId, bool isExpanded);
    bool getThreadExpanded(int threadId) const { return _threads[threadId].isExpanded; }
    void setGroupExpanded(int groupNameIdx, bool isExpanded);
    bool getGroupExpanded(int groupNameIdx) const {
        for(const Group& g : _groups) if(g.nameIdx==groupNameIdx) return g.isExpanded;
        return true;
    }
    bool getGroupAndThreadExpanded(int threadId) const {
        return _threads[threadId].isExpanded && getGroupExpanded(_threads[threadId].groupNameIdx);
    }
    void setAllExpanded(bool state) {
        for(int i=0; i<vwConst::QUANTITY_THREADID; ++i) _threads[i].isExpanded = state;
        for(Group& g : _groups) g.isExpanded = state;
        precomputeThreadExport();
    }
    void moveDragThreadId(bool srcIsGroup, int srcThreadId, int destThreadId);
    void notifyNewRecord(cmRecord* record);
    void notifyUpdatedRecord(cmRecord* record);
    const bsVec<ThreadLayout>& getLayout(void) { return _export; }
    int  getThreadGroup(int threadId) const { return _threads[threadId].groupNameIdx; }

    // Colors
    const bsVec<ImVec4>& getColorPalette(bool isLight=false) const {
        return isLight? _colorPaletteLight:_colorPaletteDark;
    }
    void   setCurveColorIdx(int elemIdx, int colorIdx) { _elems[elemIdx].colorIdx = colorIdx; _appliNeedsSaving = true; }
    int    getCurveColorIdx(int elemIdx) const { return _elems[elemIdx].colorIdx; }
    ImU32  getCurveColor   (int elemIdx, bool isLight=false) const {
        return ImColor((isLight? _colorPaletteLight:_colorPaletteDark)[_elems[elemIdx].colorIdx]);
    }
    void   randomizeCurveColors(void);
    void   randomizeThreadColors(void);
    void   setThreadColorIdx(int threadId, int colorIdx);
    int    getThreadColorIdx(int threadId) const { plAssert(threadId>=0); return _threads[threadId].colorIdx; }
    ImVec4 getThreadColor   (int threadId, bool isLight=false) const {
        if      (threadId==vwConst::LOCKS_THREADID)      return ImVec4(0.,  0.,  0.,  1.);
        else if (threadId==vwConst::CORE_USAGE_THREADID) return ImVec4(0.1, 0.1, 0.1, 1.);
        else if (threadId<0)                             return ImVec4(0.9, 0.9, 0.9, 1.);
        else return (isLight? _colorPaletteLight:_colorPaletteDark)[_threads[threadId].colorIdx];
    }

    // Curve shape
    enum CurveStyle : int { LINE, STEP, POINT, LOLLIPOP };
    void setCurvePointSize(int elemIdx, int pointSize) { _elems[elemIdx].pointSize = pointSize; _appliNeedsSaving = true; }
    int  getCurvePointSize(int elemIdx) const          { return _elems[elemIdx].pointSize; }
    void setCurveStyle(int elemIdx, CurveStyle style)  { _elems[elemIdx].style = style; _appliNeedsSaving = true; }
    CurveStyle getCurveStyle(int elemIdx) const        { return _elems[elemIdx].style; }

    // CLI history
    const bsVec<bsString>& getCliHistory(void) const { return _cliHistory; }
    bsVec<bsString>&       getCliHistory(void)       { _appliNeedsSaving = true; return _cliHistory; }

    // Lock latency
    int  getLockLatencyUs(void) const { return _lockLatencyUs; }
    bool setLockLatencyUs(int cacheMBytes);

    // Workspace layout
    struct LayoutView { int id; bsString descr; };
    struct ScreenLayout {
        bsString name;
        bsString windows;
        bsVec<LayoutView> views;
    };

    const ScreenLayout& getCurrentLayout(void) const { return _currentLayout; }
    ScreenLayout&       getCurrentLayout(void)       { _appliNeedsSaving = true; return _currentLayout; }

    // Workspace template layouts
    const bsVec<ScreenLayout>& getTemplateLayouts(void) const { return _templateLayouts; }
    bsVec<ScreenLayout>&       getTemplateLayouts(void)       { _appliNeedsSaving = true; return _templateLayouts; }

 private:
    vwConfig(const vwConst& c);

    // Private methods
    void precomputeThreadExport(void);
    void reorderThreadLayout(void);

    struct Group {
        int  nameIdx;
        bool isExpanded;
        u64  hash;
    };

    struct Elem {
        int colorIdx;
        int pointSize;
        CurveStyle style;
        u64 hash;
    };

    struct KeepAppRecordParam {
        bsString name;
        int      state;
        int      recordQty;
    };

    struct AppExtStringsPath {
        bsString name;
        bsString path;
    };

    // Common fields
    vwMain*       _main;
    bsString      _programDataPath;
    bsString      _configPath;
    bsVec<ImVec4> _colorPaletteDark;
    bsVec<ImVec4> _colorPaletteLight;
    bsVec<KeepAppRecordParam> _keepOnlyLastRecord;
    bsVec<AppExtStringsPath>   _appExtStringsPath;
    bool _globalNeedsSaving = false;
    bool _appliNeedsSaving  = false;

    // Global parameters
    int _fontSize        = 15;
    int _cacheMBytes     = 300;
    int _hWheelInversion = 0;
    int _vWheelInversion = 0;
    float _vTimelineSpacing = 0.3;
    int _winVisiCatalog  = true;
    int _winVisiRecord   = true;
    int _winVisiSearch   = false;
    int _winVisiConsole  = true;
    int _winVisiSettings = true;
    int _freezePointEnabled  = false;
    int _pauseStoringEnabled = false;
    bsString _recordStoragePath;
    bsString _lastFileImportPath;
    bsString _lastLoadedRecordPath;
    bsString _lastFileExtStringsPath;

    // Application-under-analysis specific parameters
    ThreadLayout _threads[vwConst::QUANTITY_THREADID];
    bsVec<Group> _groups;
    bsVec<int>   _order;
    bsVec<Elem>  _elems;
    bsVec<bsString> _cliHistory;
    bsVec<ThreadLayout> _export; // Precomputed for export
    bsVec<bsString> _extraLines; // Allows some persistency on temporarily non-used config lines
    bsVec<ThreadLayout> _liveConfigThreads;
    bsVec<Group>        _liveConfigGroups;
    bsVec<Elem>         _liveConfigElems;
    ScreenLayout        _currentLayout;
    bsVec<ScreenLayout> _templateLayouts;
    int                 _lockLatencyUs = 5;
};
