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

// System
#include <functional>
#include <mutex>

// External
#include "imgui.h"

// Internal
#include "bs.h"
#include "bsOs.h"
#include "bsString.h"
#include "bsTime.h"
#include "bsLockFree.h"
#include "bsHashSet.h"
#include "cmInterface.h"
#include "cmRecord.h"
#include "cmRecordIterator.h"
#include "vwConst.h"
#include "vwReplayAlloc.h"
#include "vwConfig.h"

// Forward declarations
class  cmCnx;
class  cmRecording;
class  cmLiveControl;
class  vwPlatform;
class  vwFileDialog;
struct TimelineDrawHelper;


class vwMain : public cmInterface {
public:
    // Constructor & destructor
    vwMain(vwPlatform* platform, int rxPort, const bsString& overrideStoragePath);
    ~vwMain(void);
    void notifyStart(bool doLoadLastFile);

    // Application
    void beforeDraw(bool doSaveLayout); // ImGui Layout handles layouts only at a precise moment
    void draw(void);
    const vwConfig& getConfig(void) const { return *_config; }
    vwConfig&       getConfig(void)       { return *_config; }
    cmLiveControl*  getLive(void)   const { return _live; }

    // Records
    void updateRecordList(void);

    // Interface for the record library
    bool isRecordProcessingAvailable(void) const { return _actionMode==READY; }
    bool notifyRecordStarted(const bsString& appName, const bsString& buildName, s64 timeTickOrigin, double tickToNs,
                             const cmTlvs& options);
    void notifyRecordEnded(bool isRecordOk);
    void notifyInstrumentationError(cmRecord::RecErrorType type, int threadId, u32 filenameIdx, int lineNbr, u32 nameIdx);
    void notifyErrorForDisplay(cmErrorKind kind, const bsString& errorMsg);
    void notifyNewString(const bsString& newString, u64 hash);
    bool notifyNewEvents(plPriv::EventExt* events, int eventQty);
    void notifyNewRemoteBuffer(bsVec<u8>& buffer);
    bool createDeltaRecord(void);
    void notifyCommandAnswer(plPriv::plRemoteStatus status, const bsString& answer);
    void notifyNewFrozenThreadState(u64 frozenThreadBitmap);
    void notifyNewCollectionTick(void);
    void notifyNewThread(int threadId, u64 nameHash);
    void notifyNewElem(u64 nameHash, int elemIdx, int prevElemIdx, int threadId, int flags);
    void notifyNewCli(u32 nameIdx, int paramSpecIdx, int descriptionIdx);
    void notifyFilteredEvent(int elemIdx, int flags, u64 nameHash, s64 dateNs, u64 value);

    void log(cmLogKind kind, const bsString& msg);
    void log(cmLogKind kind, const char* format, ...)
#if defined(__clang__) || defined(__GNUC__)
        __attribute__ ((format (printf, 3, 4))) // Check format at compile time
#endif
        ;

    // Window creation
    enum ProfileKind { TIMINGS, MEMORY, MEMORY_CALLS };
    void addProfileScope (int id, ProfileKind kind, int threadId, int nestingLevel, u32 scopeLIdx);
    void addProfileRange(int id, ProfileKind kind, int threadId, u64 threadUniqueHash, s64 startTimeNs, s64 timeRangeNs);
    void addHistogram  (int id, u64 threadUniqueHash, u64 hashPath,  int elemIdx, s64 startTimeNs, s64 timeRangeNs);
    void addText       (int id, int threadId, u64 threadUniqueHash=0, int startNestingLevel=0, u32 startLIdx=0);
    void addMarker     (int id, s64 startTimeNs=0);
    void addTimeline   (int id);
    void addMemoryTimeline(int id);

    // Helper methods
    void        dirty(void); // Force several frame of redrawing
    const char* getNiceDate(const bsDate& date, const bsDate& now) const;
    const char* getNiceTime(s64 ns, s64 tickNs, int bank=0) const;
    const char* getNiceDuration(s64 ns, s64 displayRangeNs=0, int bank=0) const;
    const char* getNiceByteSize(s64 byteSize) const;
    const char* getNiceBigPositiveNumber(u64 number, int bank=0) const;
    const char* getValueAsChar(int flags, double value, double displayRange=0., bool isHexa=false, int bank=0) const;
    const char* getValueAsChar(const cmRecord::Evt& e) const;
    const char* getFullThreadName(int threadId) const { return _fullThreadNames[threadId].toChar(); }
    const char* getUnitFromFlags(int flags) const;
    const char* getElemName(const bsString& baseName, int flags);
    int         getId(void);
    void        releaseId(int id);

private:
    vwMain(const vwMain& other); // To please static analyzers
    vwMain& operator=(vwMain other);

    // Component draw methods
    void drawRecord(void);
    void drawCatalog(void);
    void drawSettings(void);
    void drawMainMenuBar(void);
    void drawHelp(void);
    void drawAbout(void);
    void drawErrorMsg(void);
    void drawTimelines(void);
    void drawMemoryTimelines(void);
    void drawProfiles(void);
    void drawTexts(void);
    void drawMarkers(void);
    void drawPlots(void);
    void drawHistograms(void);

    // Record handling methods
    bool findRecord(const bsString& recordPath, int& foundAppIdx, int& foundRecIdx);
    bool loadRecord(const bsString& recordPath, int appIdx, int recIdx);
    void clearViews(void);
    void clearRecord(void);
    void removeSomeRecords(const bsVec<bsString>& recordsToDelete);
    bool updateExternalStringsFile(const bsString& appPath, const bsString& extStringFile);

    // Other shared methods
    int    getDisplayWidth(void);
    int    getDisplayHeight(void);
    bsUs_t getLastMouseMoveDurationUs(void) const { return _lastMouseMoveDurationUs; }
    void   setScopeHighlight(int threadId, double startTimeNs, double endTimeNs, int eventFlags, int nestingLevel, u32 nameIdx, bool isMultiple=false);
    void   setScopeHighlight(int threadId, double punctualTimeNs, int eventFlags, int nestingLevel, u32 nameIdx);
    bool   isScopeHighlighted(int threadId, double punctualTimeNs, int eventFlags, int nestingLevel, u32 nameIdx, bool acceptMultiple=true) {
        return (acceptMultiple || !_hlIsMultiple) && _hlThreadId!=cmConst::MAX_THREAD_QTY &&
            (_hlNameIdx==PL_INVALID || nameIdx==PL_INVALID || _hlNameIdx==nameIdx) && (_hlEventFlags<0 || eventFlags<0 || _hlEventFlags==eventFlags) &&
            (_hlNestingLevel<0 || nestingLevel<0 || _hlNestingLevel==nestingLevel) && (_hlThreadId<0 || threadId<0 || _hlThreadId==threadId) &&
            (punctualTimeNs<0. || (_hlStartTimeNs<=punctualTimeNs && punctualTimeNs<=_hlEndTimeNs));
    }
    bool   isScopeHighlighted(int threadId, double startTimeNs, double endTimeNs, int eventFlags, int nestingLevel, u32 nameIdx, bool acceptMultiple=true) {
        return (acceptMultiple || !_hlIsMultiple) && _hlThreadId!=cmConst::MAX_THREAD_QTY &&
            (_hlNameIdx==PL_INVALID || nameIdx==PL_INVALID || _hlNameIdx==nameIdx) && (_hlEventFlags<0 || eventFlags<0 || _hlEventFlags==eventFlags) &&
            (_hlNestingLevel<0 || nestingLevel<0 || _hlNestingLevel==nestingLevel) && (_hlThreadId<0 || threadId<0 || _hlThreadId==threadId) &&
            bsMin(endTimeNs, _hlEndTimeNs)>bsMax(startTimeNs, _hlStartTimeNs); // Overlap
    }
    void openHelpTooltip(int uniqueId, const char* tooltipId);
    void displayHelpText(const char* helpStr);
    void displayHelpTooltip(int uniqueId, const char* tooltipId, const char* helpStr);
    void computeTickScales(const double valueRange, const int targetTickQty, double& scaleMajorTick, double& scaleMinorTick);
    void drawSynchroGroupCombo(double comboWidth, int* syncModePtr);
    void drawTimeRuler(double winX, double winY, double winWidth, double rulerHeight, double startTimeNs, double timeRangeNs,
                       int& syncMode, double& rbWidth, double& rbStartPix, double& rbEndPix);
    bool displayPlotContextualMenu(int threadId, const char* rootText, double headerWidth=-1., double comboWidth=-1.);
    bool displayHistoContextualMenu(double headerWidth=-1., double comboWidth=-1.);
    void displayScopeTooltip(const char* titleStr, const bsVec<cmRecord::Evt>& dataChildren, const cmRecord::Evt& evt, s64 durationNs);
    void getSynchronizedRange(int syncMode, double& startTimeNs, double& timeRangeNs);
    void synchronizeNewRange(int syncMode, double startTimeNs, double timeRangeNs);
    void synchronizeText(int syncMode, int threadId, int level, int lIdx, s64 timeNs, u32 idToIgnore=(u32)-1);
    void synchronizeThreadLayout(void);
    void ensureThreadVisibility(int syncMode, int threadId);
    inline double getUpdatedRange(int deltaWheel, double newRangeNs) {
        const double scrollFactor = 1.25;
        while(deltaWheel>0) { newRangeNs /= scrollFactor; --deltaWheel; }
        while(deltaWheel<0) { newRangeNs *= scrollFactor; ++deltaWheel; }
        if(newRangeNs<1000.) newRangeNs = 1000.; // No point zooming more than this
        return newRangeNs;
    }
    void displayColorSelectMenu(const char* title, const int initialColorIdx, std::function<void(int)>& setter);

    // Structural
    vwPlatform*    _platform  = 0;
    cmCnx*         _clientCnx = 0;
    cmRecording*   _recording = 0;
    cmLiveControl* _live      = 0;
    bsVec<int>     _idPool;
    int            _idMax  = 0;
    bsString       _storagePath;
    vwConfig*      _config = 0;
    vwFileDialog*  _fileDialogExtStrings    = 0;
    vwFileDialog*  _fileDialogImport        = 0;
    vwFileDialog*  _fileDialogSelectRecord  = 0;
    bsUs_t         _lastMouseMoveDurationUs = 0;
    bool           _backgroundComputationInUse = false;

    // Window list and layout
    bool    _showHelp     = false;
    bool    _showAbout    = false;
    int     _uniqueIdFullScreen = -1;

    // Remote control
    u64  _frozenThreadBitmap = 0;

    // Draw common
    enum DragMode { NONE, DATA, BAR };
    double _mouseTimeNs = -1.;
    bool   _liveRecordUpdated = false;
    float  _lastFontSize = -1.;
    float  _timelineHeaderWidth = 200.;
    bsVec<bsString> _fullThreadNames;
    void precomputeRecordDisplay(void);

    // Highlight fields
    bool   _hlHasBeenSet   = false;
    int    _hlThreadId     = cmConst::MAX_THREAD_QTY;     // No highlight with such default value
    double _hlStartTimeNs  = 0.;
    double _hlEndTimeNs    = 0.;
    int    _hlEventFlags   = 0;
    int    _hlNestingLevel = 0;
    u32    _hlNameIdx      = 0;
    bool   _hlIsMultiple   = false;

    // Work structures (to avoid reallocation)
    bsVec<cmRecord::Evt> _workDataChildren;
    bsVec<u32>           _workLIdxChildren;
    struct RangeMenuItem {
        s64 startTimeNs;
        s64 timeRangeNs;
        bsString name;
    };
    RangeMenuItem _rangeMenuItems[4]; // For the contextual menu (empty+full+group1+group2)
    int           _rangeMenuSelection = 0;

    // Component to handle time range automatas (animation, scrolling, etc...). Used by timeline, memory and plot
    struct TimeRangeBase {
        // Fields
        int    uniqueId;
        double startTimeNs     = 0.;
        double timeRangeNs     = -1.;
        double rangeSelStartNs = 0.;
        double rangeSelEndNs   = 0.;
        DragMode dragMode      = NONE;
        int      syncMode      = 1;   // 0: isolated, 1+: group
        ImGuiID newDockId      = 0xFFFFFFFF;    // 0xFFFFFFFF: use the automatic one
        bool   didUserChangedScrollPos = false; // When changed internally, not through ImGui
        bool   isCacheDirty     = true;
        bool   isNew            = true;
        bool   isWindowSelected = true;
        bool   isTouchingEnd    = true;   // If true, live display will stick to the end
        int    ctxDraggedId         = -1; // Group & threads dragging automata
        bool   ctxDraggedIsGroup    = false;
        bool   ctxDoOpenContextMenu = false;
        u32    ctxScopeLIdx          = PL_INVALID;
        double lastWinWidth    = 0.;  // Width Change invalidates the cache (min scope resolution change)
        int    viewThreadId    = -1;  // Used in conjunction with valuePerThread
        double valuePerThread[vwConst::QUANTITY_THREADID]; // In order to control the thread visibility with the scrollbar
        // Animation
        bsUs_t animTimeUs = 0;
        double animStartTimeNs1, animStartTimeNs2;
        double animTimeRangeNs1, animTimeRangeNs2;
        // Helpers
        double getStartTimeNs(void) const { return (animTimeUs>0)? animStartTimeNs2 : startTimeNs; }
        double getTimeRangeNs(void) const { return (animTimeUs>0)? animTimeRangeNs2 : timeRangeNs; }
        bool   isAnimating(void)    const { return (animTimeUs>0); }
        void   setView(double newStartTimeNs, double newTimeRangeNs, bool noTransition=false);
        void   ensureThreadVisibility(int threadId);
        void   checkTimeBounds(double recordDurationNs);
        void   updateAnimation(void);
    };
    bool   manageVisorAndRangeSelectionAndBarDrag(TimeRangeBase& trb, bool isWindowHovered, double mouseX, double mouseY,
                                                  double winX, double winY, double winWidth, double winHeight,
                                                  bool isBarHovered, double rbWidth, double rbStartPix, double rbEndPix);
    bool   displayTimelineHeader(double yHeader, double yThreadAfterTimeline, int threadId, bool doDrawGroup, bool isDrag,
                                 bool& isThreadHovered, bool& isGroupHovered);
    void   displayTimelineHeaderPopup(TimeRangeBase& trb, int tId, bool openAsGroup);
    double getTimelineHeaderHeight(bool withGroupHeader, bool withThreadHeader);

    // Timeline
    // ========
    struct InfTlCachedScope { // 64 bytes (1 cache line)
        bool isCoarseScope;
        u32  scopeLIdx;
        s64  scopeEndTimeNs;
        s64  durationNs;
        cmRecord::Evt evt;
        double startTimePix = 0.;
        double endTimePix   = 0.;
    };
    struct TlCachedCpuPoint {
        double timePix;
        double cpuUsageRatio;
    };
    struct TlCachedCore { // 32 bytes
        bool   isCoarse;
        u16    threadId;
        u32    nameIdx;
        double startTimePix;
        double endTimePix;
        s64    durationNs;
    };
    struct TlCachedSwitch { // 32 bytes
        bool   isCoarse;
        int    coreId;
        double startTimePix;
        double endTimePix;
        s64    durationNs;
    };
    struct TlCachedSoftIrq { // 32 bytes
        bool   isCoarse;
        u32    nameIdx;
        double startTimePix;
        double endTimePix;
        s64    durationNs;
    };
    struct TlCachedLockScope { // 52+8 bytes
        bool   isCoarse;
        u8     overlappedThreadIds[vwConst::MAX_OVERLAPPED_THREAD];
        double startTimePix;
        double endTimePix;
        s64    durationNs;
        cmRecord::Evt e;
    };
    struct TlCachedLockNtf { // 36 bytes
        bool   isCoarse;
        double timePix;
        cmRecord::Evt e;
    };
    struct TlCachedLockUse {
        bsVec<TlCachedLockScope>        scopes;
        bsVec<bsVec<TlCachedLockScope>> waitingThreadScopes; // Per cmRecord::locks[].waitingThreadIds
    };
    struct TlCachedMarker {
        bool   isCoarse;
        int    elemIdx;
        double timePix;
        cmRecord::Evt e;
    };
    struct Timeline : TimeRangeBase {
        bsString getDescr(void) const;
        // Contextual menu
        int    ctxNestingLevel = 0;
        u32    ctxScopeNameIdx = (u32)-1;
        // Cache
        bsVec<TlCachedLockUse>         cachedLockUse;
        bsVec<int>                     cachedLockOrderedIdx;
        bsVec<bsVec<TlCachedLockNtf>>  cachedLockNtf;
        bsVec<bsVec<TlCachedLockScope>> cachedLockWaitPerThread;
        bsVec<bsVec<TlCachedMarker>>   cachedMarkerPerThread;
        bsVec<bsVec<TlCachedSwitch>>   cachedSwitchPerThread;
        bsVec<bsVec<TlCachedSoftIrq>>  cachedSoftIrqPerThread;
        bsVec<bsVec<TlCachedCore>>     cachedUsagePerCore;
        bsVec<TlCachedCpuPoint>        cachedCpuCurve;
        bsVec<bsVec<bsVec<InfTlCachedScope>>> cachedScopesPerThreadPerNLevel;
        void reset(void) {
            startTimeNs  = timeRangeNs = rangeSelStartNs = rangeSelEndNs = 0.;
            dragMode     = NONE;
            syncMode     = 1;
            ctxScopeLIdx   = PL_INVALID;
            isCacheDirty = true;
        }
    };
    bsVec<Timeline> _timelines;
    void prepareTimeline(Timeline& tl);
    void drawTimeline(int tlWindowIdx);
    friend struct TimelineDrawHelper;


    // Memory timeline
    // ===============
    struct MemAlloc {
        u32 allocMIdx;
        u32 vPtr;
        s64 startTimeNs;
        u32 size;
        u32 startParentNameIdx;
        u32 startNameIdx;
        u16 startLevel;
        s64 endTimeNs = -1; // -1 means "leaked"
        u32 endParentNameIdx;
        u32 endNameIdx;
        u16 endThreadId = 0xFFFF;
        u16 endLevel;
    };
    struct MemCachedPoint {
        s64    timeNs;
        double value;
        s16    level;         // For highlight
        u16    flags;
        u32    parentNameIdx;
        u32    detailNameIdx; // (u32)-1 means no detailed name
    };
    struct MemDetailListWindow {
        int      threadId;
        int      uniqueId;
        double   startTimeNs;
        double   endTimeNs;
        bsString allocScopeName;
        int      syncMode;
        bsVec<MemAlloc> allocBlocks;
        // Dynamic context
        int        sortKind   = -1;
        bool       sortToggle = false; // Bi-directional sorting state
        bsVec<int> listDisplayIdx;     // Lookup for easier reordering
    };
    struct MemFusioned {
        int x1, x2, y;
    };
    struct MemCachedThread {
        bsVec<MemCachedPoint> points;
        double maxAllocSizeValue = 0.;
    };
    struct MemoryTimeline : TimeRangeBase {
        // Virtual pointer
        double viewByteMin = +1e300;
        double viewByteMax = -1e300;
        int    allocBlockThreadId    = -1;
        double allocBlockStartTimeNs = -1.;
        double allocBlockEndTimeNs   = -1.;
        bsString allocScopeName;
        // Screen management
        float  lastScrollPos   = 0.;
        double lastWinHeight   = 0.;
        bool   doAdaptViewValueRange = false;
        bool   isPreviousRangeEmpty  = true;;
        bool   isDragging      = false;
        bsString getDescr(void) const;
        // Virtual allocation helpers
        bsVec<int>         workDeallocBlockIndexes;
        bsVec<int>         workEmptyAllocBlockIndexes;
        bsHashMap<u32,u32> workLkupAllocBlockIdx;
        bsHashMap<int,MemFusioned> workLkupFusionedBlocks;
        vwReplayAlloc     workVAlloc;
        // Cache for allocation scope details
        bsVec<MemAlloc> rawAllocBlocks;
        bsVec<int>      rawAllocBlockOrder;
        u32             maxVPtr       = 0;
        u32             startTimeVPtr = 0;
        // Cache for curves (rebuilt if dirty)
        bsVec<MemCachedThread> cachedThreadData;
        bsVec<int> cachedCallBins[2]; // Alloc, Dealloc
        double binTimeOffset = 0.;
        double maxCallQty = 0.;
    };
    bsVec<MemoryTimeline>      _memTimelines;
    bsVec<MemDetailListWindow> _memDetails;
    void prepareMemoryTimeline(MemoryTimeline& m);
    void collectMemoryBlocks(MemoryTimeline& mtl, int threadId, s64 startTimeNs, s64 endTimeNs, const bsString& scopeName,
                             bool onlyInRange, bool doAdaptViewValueRange=false);
    void drawMemoryTimeline(int memTlWindowIdx);
    void drawMemoryDetailList(int detailWindowIdx);
    friend struct MemoryDrawHelper;


    // Profile
    // =======
    struct ProfileData {
        // Node values
        bsString name;
        u32 nameIdx;
        int flags;
        int nestingLevel;
        u32 scopeLIdx;
        int callQty;
        u64 value;
        u64 childrenValue;
        bsString extraInfos;
        // Double click location
        s64 firstStartTimeNs;
        s64 firstRangeNs;
        u32 color = 0;
        // Hierarchical
        bsVec<int> childrenIndices;
    };
    struct ProfileStackItem {
        int    idx;
        int    nestingLevel;
        double startValue;
    };
    struct ProfileBuildItem {
        int parentIdx;
        int nestingLevel;
        u32 scopeLIdx;
    };
    struct ProfileBuild { // Working structure to build the profile data
        bool addFakeRootNode = false;
        bsVec<ProfileBuildItem> stack;
        bsVec<cmRecord::Evt> dataChildren, dataChildren2;
        bsVec<u32> lIdxChildren, lIdxChildren2;
        bsVec<u32> childrenScopeLIdx;
    };
    struct Profile {
        // Profile request parameters
        int         uniqueId;
        ProfileKind kind;
        s64 startTimeNs     = 0;
        s64 timeRangeNs     = 0;
        u64 threadUniqueHash = 0;
        int threadId        = -1;
        int reqNestingLevel = -1;
        u32 reqScopeLIdx     = 0;
        bsString name;
        int      computationLevel; // 100=finished, <100=under computation (not ready for drawing)
        // Data fields
        u64 totalValue;
        bsVec<ProfileData> data;
        bsVec<int> listDisplayIdx;
        // Automata
        int  syncMode     = 1;          // 0 = isolated, 1+ = group
        ImGuiID newDockId = 0xFFFFFFFF; // 0xFFFFFFFF: use the automatic one
        bool isFlameGraph = false;
        bool isFlameGraphDownward = true;
        bool isWindowSelected = true;
        bool isNew        = true;
        bool isFirstRun   = true;
        int  cmDataIdx    = -1;
        bool isDragging   = false;
        bsVec<ProfileStackItem> workStack;
        u32  lastSearchedNameIdx = (u32)-1;
        int  lastSearchedItemIdx = -1;
        // FlameGraph
        bsString callName;
        int      maxDepth   = 0;
        double   minRange   = 1000.;
        double   startValue = 0.;
        double   endValue;
        int      maxNestingLevel = 0;
        DragMode dragMode    = NONE;
        double selStartValue = 0.;
        double selEndValue   = 0.;
        bsUs_t animTimeUs    = 0;
        double animStartValue1, animStartValue2;
        double animEndValue1,   animEndValue2;
        // Methods
        void init(const bsString& name_, s64 startTimeNs_, s64 timeRangeNs_, int threadId_, int uniqueId_) {
            name = name_; startTimeNs = startTimeNs_; timeRangeNs = timeRangeNs_;
            threadId = threadId_; uniqueId = uniqueId_;
            data.reserve(512);
        }
        bsString getDescr(void) const;
        void   notifySearch(u32 searchedNameIdx);
        double getStartValue(void) const { return (animTimeUs>0)? animStartValue2 : startValue; }
        double getEndValue(void)   const { return (animTimeUs>0)? animEndValue2   : endValue; }
        void   setView(double newStartValue, double newEndValue) {
            animStartValue1 = startValue; animStartValue2 = newStartValue;
            animEndValue1   = endValue;   animEndValue2   = newEndValue;
            bsUs_t currentTimeUs = bsGetClockUs();
            animTimeUs = (animTimeUs==0)? currentTimeUs : currentTimeUs-bsMin((bsUs_t)(0.5*vwConst::ANIM_DURATION_US), currentTimeUs-animTimeUs);
        }
    };
    ProfileBuild   _profileBuild; // Used only when computating a new profile
    bsVec<Profile> _profiles;
    int            _profiledCmDataIdx = -1;
    void _addProfileStack(Profile& prof, const bsString& name, s64 startTimeNs, s64 timeRangeNs,
                          bool addFakeRootNode, int startNestingLevel, bsVec<u32> scopeLIndexes);
    bool _computeChunkProfileStack(Profile& prof);
    void _drawTextProfile(Profile& prof);
    void _drawFlameGraph(bool doDrawDownward, Profile& prof);

    // Text windows
    // ============
    struct TextCacheItem {
        cmRecord::Evt evt;
        s64 scopeEndTimeNs; // Only for "begin scope" events, else N/A
        int nestingLevel;
        u32 lIdx;
        int elemIdx;
    };
    struct Text {
        int uniqueId;
        int threadId;
        u64 threadUniqueHash;
        int startNLevel;
        u32 startLIdx;
        float  lastScrollPos = 0.;
        double lastWinHeight = 0.; // Any growth dirties the cache (need more lines...)
        int  syncMode        = 1;  // 0 = isolated, 1+ = group
        ImGuiID newDockId    = 0xFFFFFFFF;       // 0xFFFFFFFF: use the automatic one
        bool didUserChangedScrollPos    = false; // When changed internally (arrow keys, drag...)
        bool didUserChangedScrollPosExt = false; // When changed externally (timeline synchro...)
        bool isWindowSelected = true;
        bool isCacheDirty   = true;
        bool isNew          = true;
        bool isFirstRun     = true;
        bool isDragging     = false;
        double firstTimeNs  = 0.;
        double lastTimeNs   = 0.;
        double dragReminder = 0.;
        bsHashSet hiddenSet;
        // Contextual menu
        int ctxNestingLevel = 0;
        int ctxScopeLIdx    = 0;
        u32 ctxNameIdx      = 0;
        int ctxFlags        = 0;
        // Cache (rebuilt if dirty)
        double cachedScrollRatio = 0.;
        bsVec<TextCacheItem> cachedItems;
        bsVec<cmRecordIteratorHierarchy::Parent> cachedStartParents;
        // Helpers
        bsString getDescr(void) const;
        void setStartPosition(int nestingLevel, u32 lIdx, int idToIgnore=-1) {
            if(idToIgnore==uniqueId) return;
            if(nestingLevel==startNLevel && lIdx==startLIdx) return;
            startNLevel  = nestingLevel;
            startLIdx    = lIdx;
            isCacheDirty = true;
            isWindowSelected = true;
        }
        bool isHidden(int nestingLevel, u64 nameHash) {
            return hiddenSet.find(bsHashStep(((u64)nestingLevel<<32) | nameHash));
        }
        void setHidden(bool state, int nestingLevel, u64 nameHash) {
            u64 key = bsHashStep(((u64)nestingLevel<<32) | nameHash);
            if(state) hiddenSet.set(key);
            else      hiddenSet.unset(key);
        }
    };
    bsVec<Text> _texts;
    void prepareText(Text& t);
    void drawText(Text& t);

    // Marker windows
    // ==============
    struct MarkerCacheItem {
        cmRecord::Evt evt;
        int elemIdx;
    };
    struct Marker {
        int uniqueId;
        int startIdx = 0;
        int maxIdx   = 1;
        int maxCategoryLength   = 1;
        int maxThreadNameLength = 1;
        float  lastScrollPos = 0.;
        double lastWinHeight = 0.; // Any growth dirties the cache (need more lines...)
        int    syncMode      = 1;  // 0 = isolated, 1+ = group
        ImGuiID newDockId    = 0xFFFFFFFF;         // 0xFFFFFFFF: use the automatic one
        bool   didUserChangedScrollPos    = false; // When changed internally (arrow keys, drag...)
        bool   didUserChangedScrollPosExt = false; // When changed externally (timeline synchro...)
        bool   isNew        = true;
        bool   isCacheDirty = true;
        bool   isWindowSelected = true;
        bool   isDragging     = false;
        double dragReminder   = 0.;
        s64    forceTimeNs    = -1;
        bsVec<bool> threadSelection;
        bsVec<bool> categorySelection;
        bool isFilteredOnThread   = false;
        bool isFilteredOnCategory = false;
        // Contextual menu
        int  ctxThreadId     = 0;
        u32  ctxNameIdx      = 0;
        // Cache (rebuilt if dirty)
        double cachedScrollRatio = 0.;
        bsVec<MarkerCacheItem> cachedItems;
        // Helpers
        bsString getDescr(void) const;
        void setStartPosition(s64 timeNs, int idToIgnore=-1, bool doSelectWindow=false) {
            if(idToIgnore==uniqueId) return;
            forceTimeNs  = timeNs;
            isCacheDirty = true;
            didUserChangedScrollPosExt = true;
            isWindowSelected = doSelectWindow;
        }
        bool isFiltered(int threadId, int categoryId) {
            return ((threadId<threadSelection    .size() && !threadSelection[threadId]) ||
                    (categoryId<categorySelection.size() && !categorySelection[categoryId]));
        }
    };
    bsVec<Marker> _markers;
    void prepareMarker(Marker& t);
    void drawMarker(Marker& t);

    // Search windows
    // ==============
    struct SearchCacheItem {
        cmRecord::Evt evt;
        s64    timeNs;
        double value;
        int    elemIdx;
        u32    lIdx;
    };
    struct SearchAggregatedIterator {
        void init(cmRecord* record, u32 selectedNameIdx, u64 threadBitmap, s64 initStartTimeNs,
                  int itemMaxQty, bsVec<SearchCacheItem>& items);
        s64  getPreviousTime(int itemQty);
        s64  startTimeNs = 0;
        bsVec<cmRecordIteratorElem> itElems;
        bsVec<SearchCacheItem>      itElemsEvts;
    };
    struct Search {
        int uniqueId;
        int startIdx = 0;
        int maxIdx   = 1;
        char input[128] = { 0 };
        bool isInputCaseSensitive = false;
        bool isInputPopupOpen  = false;
        bool isCompletionDirty = true;
        int  completionIdx     = -1;
        bsVec<u32> completionNameIdxs;
        u32  selectedNameIdx     = (u32)-1;
        bsVec<bool> threadSelection;
        bool isFilteredOnThread  = false;
        int  maxThreadNameLength = 1;
        float  lastScrollPos = 0.;
        double lastWinHeight = 0.; // Any growth dirties the cache (need more lines...)
        int    syncMode      = 1;  // 0 = isolated, 1+ = group
        ImGuiID newDockId    = 0xFFFFFFFF;         // 0xFFFFFFFF: use the automatic one
        bool   didUserChangedScrollPos    = false; // When changed internally (arrow keys, drag...)
        bool   didUserChangedScrollPosExt = false; // When changed externally (timeline synchro...)
        bool   isNew        = true;
        bool   isCacheDirty = true;
        bool   isWindowSelected = true;
        bool   isDragging   = false;
        double dragReminder = 0.;
        double lastMouseY   = 0.;
        s64    forceTimeNs  = -1;
        s64    startTimeNs  = 0;
        // Contextual menu
        int  ctxThreadId     = 0;
        int  ctxNestingLevel = 0;
        int  ctxScopeLIdx     = 0;
        u32  ctxNameIdx      = 0;
        // Cache (rebuilt if dirty)
        double cachedScrollRatio = 0.;
        bsVec<SearchCacheItem> cachedItems;
        SearchAggregatedIterator aggregatedIt;
        // Helpers
        void setStartPosition(s64 timeNs, int idToIgnore=-1) {
            if(idToIgnore==uniqueId) return;
            forceTimeNs  = timeNs;
            isCacheDirty = true;
            didUserChangedScrollPosExt = true;
        }
        void reset(void) {
            input[0] = 0;
            isInputPopupOpen   = false;
            isCompletionDirty  = true;
            completionIdx      = -1;
            completionNameIdxs.clear();
            cachedItems.clear();
        }
    };
    Search _search;
    void prepareSearch(void);
    void drawSearch(void);

    // Plot/Histogram selection menu
    // =============================
    struct PlotMenuItem {
        bsString name;
        bsString unit;
        int      elemIdx;
        u32      nameIdx;  // For "all" plots with same name
        int      flags;
        bsVec<int> existingPlotWindowIndices;
        s64        startTimeNs;
        s64        timeRangeNs;
        bsString comboSelectionString;
        int      comboSelectionExistingIdx = -1;
        int      comboSelectionNewIdx      = -1;
        bool     comboSelectionRemoval     = false;
        bsString comboHistoSelectionString;
        int      comboHistoSelectionIdx = -1;
    };
    bsVec<PlotMenuItem> _plotMenuItems;
    bsVec<bsString>     _plotMenuNewPlotUnits;
    bsVec<int>          _plotMenuNewPlotCount;
    bool                _plotMenuWithRemoval;
    int                 _plotMenuSpecificCurveIdx;
    double              _plotMenuNamesWidth  = 0.;
    bool                _plotMenuAddAllNames = true;
    bool                _plotMenuHasScopeChildren = false;
    bool                _plotMenuIsPartOfHStruct = false;
    u64                 _plotMenuThreadUniqueHash = 0;


    // Plot windows
    // =============
    struct PlotCachedPoint {
        s64    timeNs;
        double value;
        u32    lIdx;
        cmRecord::Evt evt;
    };
    struct PlotCurve {
        u64  threadUniqueHash;
        u64  hashPath;
        int  elemIdx;
        bool isEnabled;
        bool isHexa;
        // Cached
        double absYMin = +1e300; // Absolute min/max (should not change too much after initial global range)
        double absYMax = -1e300;
    };
    struct PlotWindow : TimeRangeBase {
        bsVec<PlotCurve> curves;
        bsString unit = "";
        double   valueMin   = +1e300;
        double   valueMax   = -1e300;
        float    legendPosX = 0.8; // Window ratio coordinates of the center
        float    legendPosY = 0.05;
        DragMode legendDragMode = NONE;
        float    lastScrollPos  = 0.;
        float    lastWinHeight  = 0.;
        bool     doShowPointTooltip = false;
        bool     isUnitSet  = false;
        bool     isFirstRun = true;
        bsString getDescr(void) const;
        // Cache (rebuilt if dirty)
        bsVec<bsVec<PlotCachedPoint>> cachedItems;
        bsVec<bsString> curveNames;
        bsVec<bsString> curveThreadNames;
        double maxWidthCurveName;
        double maxWidthThreadName;
    };
    bsVec<PlotWindow> _plots;
    void preparePlot(PlotWindow& t);
    void drawPlot(int plotWindowIdx);
    bool prepareGraphContextualMenu(int threadId, int nestingLevel, u32 lIdx, s64 startTimeNs, s64 timeRangeNs,
                                    bool withChildren=true, bool withRemoval=false);
    void prepareGraphContextualMenu(int elemIdx, s64 startTimeNs, s64 timeRangeNs, bool addAllNames, bool withRemoval);


    // Histogram
    // =========
    struct HistoData {
        u32 qty;
        u32 cumulQty;
        int threadId;
        u32 lIdx;   // Of the highest/lowest value for this cell
        s64 timeNs; // Of the highest/lowest value for this cell (depending on the plot config)
    };
    struct HistogramBuild {
        double absMinValue = +1e300;
        double absMaxValue = -1e300;
        bsVec<double> maxValuePerBin;
        cmRecordIteratorElem   itGen;
        cmRecordIteratorMarker itMarker;
        cmRecordIteratorLockNtf itLockNtf;
        cmRecordIteratorLockUseGraph itLockUse;
    };
    struct Histogram {
        // Parameters
        int      uniqueId;
        int      elemIdx;
        u64      threadUniqueHash;
        u64      hashPath;
        bsString name;
        s64      startTimeNs;
        s64      timeRangeNs;
        int      computationLevel;
        bool     isHexa;
        // View
        double   viewZoom      = 1.;
        double   viewStartX    = 0.;
        double   fsCumulFactor = -1.; // Indirect parametrization of the bin qty
        int      rangeSelStartIdx = 0;
        int      rangeSelEndIdx   = 0;
        DragMode dragMode      = NONE;
        double   lastWinWidth  = 0.;
        int      syncMode      = 1; // 0: isolated, 1+: group
        ImGuiID  newDockId     = 0xFFFFFFFF; // 0xFFFFFFFF: use the automatic one
        float    legendPosX = 0.8; // Window ratio coordinates of the center
        float    legendPosY = 0.05;
        DragMode legendDragMode = NONE;
        bool     isCacheDirty   = true;
        bool     isFirstRun     = true;
        bool     isNew          = true;
        bool     isWindowSelected = true;
        // Constant data
        bsVec<HistoData> fullResData;
        double absMinValue;
        double absMaxValue;
        u32    totalQty = 0;
        // Cache
        double deltaY;
        u32    maxQty;
        bsVec<HistoData> data;
        bsVec<int> discreteLkup; // Used only in case of discrete values (i.e. strings)
        bsString getDescr(void) const;
        void checkBounds(void);
    };
    HistogramBuild   _histoBuild;
    bsVec<Histogram> _histograms;
    void prepareHistogram(Histogram& h);
    void drawHistogram(int histogramIdx);
    bool _computeChunkHistogram(Histogram& h);

    // Log console
    // ===========
    struct LogItem {
        cmLogKind kind;
        s8        hour, minute, second;
        bsString  text;
    };
    struct LogConsole {
        int  uniqueId;
        bool isVisible = false;
        int  firstIdx  = 0;
        bsVec<LogItem> logs;
        std::mutex     logMx; // log() may be called from any thread
    };
    LogConsole _logConsole;
    void drawLogConsole(void);

    // Catalog
    // =======
    struct RecordInfos {
        int      idx;
        bsString path;
        u64      size;
        bsDate   date;
        char     nickname[32];
    };
    struct AppRecordInfos {
        int      idx = -1;
        bsString path;
        u64      size;
        bsString name;
        bsVec<RecordInfos> records;
    };
    bsVec<AppRecordInfos> _cmRecordInfos;
    int _underRecordAppIdx  = -1;
    int _underRecordRecIdx  = -1;
    int _underDisplayAppIdx = -1;
    int _underDisplayRecIdx = -1;
    int _forceOpenAppIdx    = 0;
    struct CatalogWindow {
        int     uniqueId;
        ImGuiID newDockId        = 0xFFFFFFFF;   // 0xFFFFFFFF: use the automatic one
        bool    isNew            = true;
        bool    isWindowSelected = true;
        int     headerAction     = 0;  // 1=open  2=close
    };
    CatalogWindow _catalogWindow;

    // Record
    // ======
    struct RecordWindow {
        int     uniqueId;
        ImGuiID newDockId       = 0xFFFFFFFF;   // 0xFFFFFFFF: use the automatic one
        bool   isNew            = true;
        bool   isWindowSelected = true;
        bool   doForceShowLive  = true;
    };
    RecordWindow _recordWindow;
    cmRecord*    _record = 0;

    // Settings
    // ========
    struct SettingsWindow {
        int     uniqueId;
        ImGuiID newDockId        = 0xFFFFFFFF;   // 0xFFFFFFFF: use the automatic one
        bool    isNew            = true;
        bool    isWindowSelected = true;
    };
    SettingsWindow _settingsWindow;

    // UI Layout
    int      _uniqueIdHelp = -1;  // State of the help popup automata
    int      _nextUniqueIdFullScreen = -2; // -2 = idle, -1 = disable full screen, >=0 = activate full screen
    bool     _doEnterFullScreen = false;
    bool     _doCreateNewViews = false;
    bsString _fullScreenLayoutDescr;
    void selectBestDockLocation(bool bigWidth, bool bigHeight);
    void setFullScreenView(int uniqueId);
    void copyCurrentLayout(vwConfig::ScreenLayout& layout, const bsString& windowLayout);
    void createLayoutViews(const vwConfig::ScreenLayout& layout);

    // Actions
    enum ActionMode { READY, ERROR_DISPLAY, START_RECORD, END_RECORD, LOAD_RECORD };
    ActionMode       _actionMode = READY;
    bsVec<bsString>  _recordsToDelete;
    bool             _doClearRecord = false;
    int              _waitForDisplayRefresh = 0;
    vwConfig::ScreenLayout _screenLayoutToApply;
    bsString         _doSaveTemplateLayoutName;

    // Inter-thread messages
    struct MsgRecord { bsString recordPath; };
    struct MsgError  { cmErrorKind kind; bsString msg; };
    bsMsgExchanger<cmRecord*>  _msgRecordStarted;
    bsMsgExchanger<bool>       _msgRecordEnded;
    bsMsgExchanger<MsgRecord>  _msgRecordLoad;
    bsMsgExchanger<MsgError>   _msgRecordErrorDisplay;
    bsMsgExchanger<cmRecord::Delta> _msgRecordDelta;
    MsgError   _safeErrorMsg;
    MsgRecord* _recordLoadSavedMsg = 0; // 2 steps load automata
};
