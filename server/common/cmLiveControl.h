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

// System
#include <mutex>

// Internal
#include "palanteer.h"
#include "bs.h"
#include "bsVec.h"
#include "bsString.h"

// Forward declarations
class cmInterface;
class cmCnx;

class cmLiveControl {
public:
    cmLiveControl(cmInterface* main, cmCnx* clientCnx);
    ~cmLiveControl(void);

    // Notifications
    void storeNewRemoteBuffer(bsVec<u8>& buffer);

    // Commands
    bool remoteSetMaxLatencyMs(int latencyMs);
    bool remoteSetFreezeMode(bool state);
    bool remoteStepContinue(u64 bitmap=(u64)-1L);
    bool remoteKillProgram(void);
    bool remoteCli(const bsVec<bsString>& commands);

private:
    bsVec<u8>* _prepareCommand(enum plPriv::RemoteCommandType ct, int payloadSize);

    cmInterface* _itf = 0;
    cmCnx* _clientCnx = 0;
};
