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
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

// Internal
#include "bs.h"
#include "bsVec.h"
#include "bsTime.h"
#include "bsString.h"
#include "bsNetwork.h"
#include "bsLockFree.h"

#define MAX_REMOTE_COMMAND_BYTE_SIZE 32*1024

class cmInterface;

class cmCnx {
public:
    cmCnx(cmInterface* itf, int port);
    ~cmCnx(void);

    void runRxFromClient(void);
    void runTxToClient(void);
    void injectFile(const bsString& filename);

    bsVec<u8>* getTxBuffer (void);
    void       sendTxBuffer(void);

private:
    cmCnx(const cmCnx& other); // To please static analyzers
    cmCnx& operator=(cmCnx other);

    // Private methods
    bool initializeTransport(FILE* fd);
    bool parseTransportLayer(u8* buf, int qty);
    bool processNewEvents(u8* buf, int eventQty);
    void dataReceptionLoop(FILE* fd);

    // Both direction
    cmInterface*             _itf;
    int                      _port;
    std::atomic<int>         _doStopThreads;
    bsSocket_t               _clientSocket = bsSocketError;
    bool                     _rxIsStarted = false;
    bool                     _txIsStarted = false;
    std::mutex               _threadInitMx;
    std::condition_variable  _threadInitCv;
    bsMsgExchanger<bsString> _msgInjectFile;

    // Reception
    static constexpr int _recBufferSize = 256000;
    u8*          _recBuffer = 0;
    std::thread* _threadClientRx = 0;
    bsUs_t       _lastDeltaRecordTime = 0;

    // Parsing
    bool   _recordToggleBytes  = false;
    bool   _areStringsExternal = false;
    bool   _isStringHashShort  = false;
    bool   _isControlEnabled   = true;
    bool   _isDateShort        = false;
    bool   _isCompactModel     = false;
    int    _recordProtocol     = 0;
    s64    _timeTickOrigin;
    double _tickToNs;
    bsString _appName;
    bsString _buildName;
    bsVec<u8> _conversionBuffer;
    static constexpr int _parseHeaderSize = 8;
    int _parseHeaderDataLeft = _parseHeaderSize;
    int _parseStringLeft = 0;
    int _parseEventLeft  = 0;
    int _parseRemoteLeft = 0;
    bsVec<u8> _parseTempStorage;
    bool _isCollectionTick = false;
    void resetParser(void) {
        _parseHeaderDataLeft = _parseHeaderSize; _parseStringLeft = 0;
        _parseEventLeft = 0; ; _parseRemoteLeft = 0; _parseTempStorage.clear();
        _isCollectionTick = false;
    }

    // Transmission
    std::thread* _threadClientTx = 0;
    std::mutex   _threadWakeUpMx;
    std::condition_variable _threadWakeUpCv;
    bsVec<u8>        _txBuffer;
    std::atomic<int> _txBufferState; // 0=free  1=under fill   2=sending
};
