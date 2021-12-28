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
    bool setMaxLatencyMs(int maxLatencyMs) { return _live->remoteSetMaxLatencyMs(0, maxLatencyMs); }
    bool setFreezeMode(bool state) { return _live->remoteSetFreezeMode(0, state); }
    bool stepContinue (u64 bitmap) { return _live->remoteStepContinue(0, bitmap); }
    bool killProgram(void)         { return _live->remoteKillProgram(0); }
    bool cli(const bsVec<bsString>& commands) { return _live->remoteCli(0, commands); }
    void clearAllSpecs(void);
    void clearBufferedEvents(void);
    void addSpec(const char* threadName, u64 threadHash, pyiSpec* parentPath, pyiSpec* elemArray, int elemQty);
    void setRecordFilename(const char* recordFilename);  // Zero means no recording
    void getUnresolvedElemInfos(pyiDebugSpecInfo** infoArray, int* infoQty);

    // Interface for the common library
    void logToConsole(cmLogKind kind, const bsString& msg);
    void logToConsole(cmLogKind kind, const char* format, ...)
#if defined(__clang__) || defined(__GNUC__)
        __attribute__ ((format (printf, 3, 4))) // Check format at compile time
#endif
        ;
    bool isRecordProcessingAvailable(void) const { return true; }
    bool isMultiStreamEnabled(void) const { return false; }
    bool notifyRecordStarted(const cmStreamInfo& infos, s64 timeTickOrigin, double tickToNs);
    void notifyRecordEnded(bool isRecordOk);
    void notifyInstrumentationError(cmRecord::RecErrorType type, int threadId, u32 filenameIdx, int lineNbr, u32 nameIdx);
    void notifyErrorForDisplay(cmErrorKind kind, const bsString& errorMsg);
    void notifyNewStream(const cmStreamInfo& infos);
    void notifyNewString(int streamId, const bsString& newString, u64 hash);
    bool notifyNewEvents(int streamId, plPriv::EventExt* events, int eventQty, s64 shortDateSyncTick);
    void notifyNewRemoteBuffer(int streamId, bsVec<u8>& buffer);
    bool createDeltaRecord(void);
    void notifyCommandAnswer(int streamId, plPriv::plRemoteStatus status, const bsString& answer);
    void notifyNewFrozenThreadState(int streamId, u64 frozenThreadBitmap);
    void notifyNewCollectionTick(int streamId);
    void notifyNewThread(int threadId, u64 nameHash);
    void notifyNewElem(u64 nameHash, int elemIdx, int prevElemIdx, int threadId, int flags);
    void notifyNewCli(int streamId, u32 nameIdx, int paramSpecIdx, int descriptionIdx);
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
    bsString _recordFilename; // Empty means no recording

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
