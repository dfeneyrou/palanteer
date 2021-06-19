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

#include "bs.h"

struct pyiString {
    u64         nameHash;
    const char* name;
};

struct pyiThread {
    u64 nameHash;
    int threadId;
};

struct pyiElem {
    u64 nameHash;
    int elemIdx;
    int prevElemIdx;
    int threadId;
    int flags;
};

struct pyiEvent {
    int specId;
    int elemId;
    int childrenQty;
    u64 nameHash;
    s64 dateNs; // If applicable
    u64 value;  // Placeholder to cast in the corresponding type
};

struct pyiSpec {
    const char** path;
    int pathQty;
};

struct pyiCli {
    const char* name;
    const char* paramSpec;
    const char* description;
};

struct pyiDebugSpecInfo {
    int  specId;
    int  elemId;
    char errorMsg[64];
};

// Define the notifications
struct pyiNotifications {
    void (*notifyRecordStarted)(const char* appName, const char* buildName, bool areStringsExternal, bool isStringHashShort, bool isControlEnabled);
    void (*notifyRecordEnded)(void);
    void (*notifyLog)(int level, const char* msg);
    void (*notifyCommandAnswer)(int status, const char* answer);
    void (*notifyNewFrozenThreadState)(u64 frozenThreadBitmap);
    void (*notifyNewStrings)(pyiString* strings, int stringQty);
    void (*notifyNewCollectionTick)(void);
    void (*notifyNewThreads)(pyiThread* threads, int threadQty);
    void (*notifyNewElems  )(pyiElem* elems, int elemQty);
    void (*notifyNewClis   )(pyiCli* clis, int cliQty);
    void (*notifyNewEvents )(pyiEvent* events, int eventQty);
};

// Library initialization
bool pyiInitialize(int rxPort, pyiNotifications ntf);
bool pyiUninitialize(void);

// Config
void pyiSetRecordFilename(const char* filenameOrEmpty); // Default is no recording. If empty (""), disables recording
void pyiSetFreezeModeState(bool state); // False as a default. Fully dynamic

// Commands
void pyiSendCliRequest(const char* commandStr);
void pyiStepContinue(u64 threadBitmap);
void pyiKillProgram(void);

// Event specs
void pyiClearBufferedEvents(void);
void pyiClearAllSpecs(void);
void pyiAddSpec(const char* threadName, pyiSpec* parentPath, pyiSpec* elemArray, int elemQty);
void pyiGetUnresolvedElemInfos(pyiDebugSpecInfo** infoArray, int* infoQty);
