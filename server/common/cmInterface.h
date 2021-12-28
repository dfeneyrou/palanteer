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

// Internal
#include "bsString.h"
#include "cmRecord.h"

// Enumerations & types
enum cmLogKind : u8 { LOG_DETAIL, LOG_INFO, LOG_WARNING, LOG_ERROR };  // @#RENAME with cmLogMsgKind, to make it less ambiguous with the event logs
enum cmErrorKind { ERROR_LOAD, ERROR_IMPORT, ERROR_GENERIC };


// Interface
class cmInterface {
 public:
    virtual ~cmInterface(void) {}

    // Logging
    virtual void logToConsole(cmLogKind kind, const bsString& msg) = 0;
    virtual void logToConsole(cmLogKind kind, const char* format, ...)
#if defined(__clang__) || defined(__GNUC__)
        __attribute__ ((format (printf, 3, 4))) // Check format at compile time
#endif
        = 0;

    // Recording pipeline readiness
    virtual bool isRecordProcessingAvailable(void) const = 0;
    virtual bool isMultiStreamEnabled(void) const = 0;

    // Notifications for recording and remote control
    virtual bool notifyRecordStarted(const cmStreamInfo& infos, s64 timeTickOrigin, double tickToNs) = 0;
    virtual void notifyRecordEnded(bool isRecordOk) = 0;
    virtual void notifyInstrumentationError(cmRecord::RecErrorType type, int threadId, u32 filenameIdx, int lineNbr, u32 nameIdx) = 0;
    virtual void notifyErrorForDisplay(cmErrorKind kind, const bsString& errorMsg) = 0;
    virtual void notifyNewStream(const cmStreamInfo& infos) = 0;
    virtual void notifyNewString(int streamId, const bsString& newString, u64 hash) = 0;
    virtual bool notifyNewEvents(int streamId, plPriv::EventExt* events, int eventQty, s64 shortDateSyncTick) = 0;
    virtual void notifyNewRemoteBuffer(int streamId, bsVec<u8>& buffer) = 0;
    virtual bool createDeltaRecord(void) = 0;
    virtual void notifyCommandAnswer(int streamId, plPriv::plRemoteStatus status, const bsString& answer) = 0;
    virtual void notifyNewFrozenThreadState(int streamId, u64 frozenThreadBitmap) = 0;

    // Notifications for scripting
    virtual void notifyNewCollectionTick(int streamId) = 0;
    virtual void notifyNewThread(int threadId, u64 nameHash) = 0;
    virtual void notifyNewElem(u64 nameHash, int elemIdx, int prevElemIdx, int threadId, int flags) = 0;
    virtual void notifyNewCli(int streamId, u32 nameIdx, int paramSpecIdx, int descriptionIdx) = 0;
    virtual void notifyFilteredEvent(int elemIdx, int flags, u64 nameHash, s64 dateNs, u64 value) = 0;
};
