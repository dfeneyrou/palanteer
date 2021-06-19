// Palanteer scripting library
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
#include <mutex>

// Internal
#include "cmInterface.h"
#include "cmLiveControl.h"
#include "pyInterface.h"

// Forward declarations
class  cmCnx;
class  cmRecord;
class  cmRecording;
class  cmLiveControl;


class pyMainItf : public cmInterface {
public:
    // Constructor & destructor
    pyMainItf(int rxPort, pyiNotifications ntf);
    ~pyMainItf(void);

    // Interface for the python binding
    bool setMaxLatencyMs(int maxLatencyMs) { return _live->remoteSetMaxLatencyMs(maxLatencyMs); }
    bool setFreezeMode(bool state) { return _live->remoteSetFreezeMode(state); }
    bool stepContinue (u64 bitmap) { return _live->remoteStepContinue(bitmap); }
    bool killProgram(void)         { return _live->remoteKillProgram(); }
    bool cli(const bsVec<bsString>& commands) { return _live->remoteCli(commands); }
    void clearAllSpecs(void);
    void clearBufferedEvents(void);
    void addSpec(const char* threadName, pyiSpec* parentPath, pyiSpec* elemArray, int elemQty);
    void setRecordingConfig(bool isEnabled, const char* forcedFilename);
    void getUnresolvedElemInfos(pyiDebugSpecInfo** infoArray, int* infoQty);

    // Interface for the common library
    void log(cmLogKind kind, const bsString& msg);
    void log(cmLogKind kind, const char* format, ...)
#if defined(__clang__) || defined(__GNUC__)
        __attribute__ ((format (printf, 3, 4))) // Check format at compile time
#endif
        ;
    bool notifyRecordStarted(const bsString& appName, const bsString& buildName, int protocol, s64 timeNsOrigin, double tickToNs,
                             bool areStringsExternal, bool isStringHashShort, bool isControlEnabled);
    void notifyRecordEnded(void);
    void notifyInstrumentationError(cmRecord::RecErrorType type, int threadId, u32 filenameIdx, int lineNbr, u32 nameIdx);
    void notifyErrorForDisplay(cmErrorKind kind, const bsString& errorMsg);
    void notifyNewString(const bsString& newString, u64 hash);
    void notifyNewEvents(plPriv::EventExt* events, int eventQty);
    void notifyNewRemoteBuffer(bsVec<u8>& buffer);
    bool createDeltaRecord(void);
    void notifyCommandAnswer(plPriv::plRemoteStatus status, const bsString& answer);
    void notifyNewFrozenThreadState(u64 frozenThreadBitmap);
    void notifyNewCollectionTick(void);
    void notifyNewThread(int threadId, u64 nameHash);
    void notifyNewElem(u64 nameHash, int elemIdx, int prevElemIdx, int threadId, int flags);
    void notifyNewCli(u32 nameIdx, int paramSpecIdx, int descriptionIdx);
    void notifyFilteredEvent(int elemIdx, int flags, u64 nameHash, s64 dateNs, u64 value);

 private:

    // Structural
    cmCnx*         _clientCnx = 0;
    cmRecording*   _recording = 0;
    cmLiveControl* _live      = 0;
    cmRecord*      _record    = 0;
    std::mutex     _mx;
    pyiNotifications* _ntf   = 0;
    bool _isRecordOnGoing     = false;
    bool _isStringHashShort   = false;
    bool _areStringsExternal  = false;

    // Batch notifications
    bsVec<pyiString> _batchedStrings;
    bsVec<pyiThread> _batchedThreads;
    bsVec<pyiElem>   _batchedElems;
    bsVec<pyiCli>    _batchedClis;
    bsVec<pyiEvent>  _batchedEvents;
    bool             _didCollectionTickOccurred = false;

    // Event specs
    enum ResolutionState { NO_ELEMENTS_SEEN, NO_MATCHING_THREAD, NO_MATCHING_NAME, NO_MATCHING_PATH,
                           NO_MATCHING_PARENT_NAME, NO_MATCHING_PARENT_PATH, NO_MATCHING_ELEM_ROOT, NO_MATCHING_PARENT_ROOT,
                           INCONSISTENT_PARENT, RESOLVED, RESOLUTION_STATE_QTY };
    struct SpecElemToken {
        bsString name;
        u64      tokenType; // 0: elem hash   1: "*" skip 1 level   2: "**" multi-level wildcard   3: "." exactly the parent
        u64      hash = 0;
    };
    struct SpecElem {
        bsVec<SpecElemToken> tokens;
        ResolutionState resolution = NO_ELEMENTS_SEEN;
    };
    struct Spec {
        // Definition
        bsString threadName;
        u64      threadHash = 0;
        bsVec<SpecElemToken> parentPath;
        bsVec<SpecElem>      elems;
        // Resolution
        int parentElemIdx = -1;
        // Working fields
        bool isOpenParent = false;
        bsVec<pyiEvent> events;
    };
    struct ElemSpecCtx {
        int       specId;
        pyiEvent beginEvent; // Temporary for scope events, waiting for "end"
    };
   struct ElemCtx {
       bsVec<ElemSpecCtx> specs;
       u64                nameHash;
       s64                lastBeginDateNs = 0;
       bool               isActive        = false;
       bool               isDeclared      = false;
       void reset(void) { specs.clear(); isActive = false; }
    };
    s64              _lastDateNs = 0;
    bsVec<Spec>      _specs;
    bsVec<ElemCtx>   _elemSpecContexts;
    bsVec<pyiDebugSpecInfo> _debugSpecInfos;
    ResolutionState matchPath(int& startElemIdx, const bsVec<SpecElemToken>& tokens);
    void computeSpecHashes(Spec& f);
    void resolveSpecs(int elemIdx, int threadId);
    void notifyScript(void);
};
