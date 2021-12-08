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
#include "bsString.h"
#include "bsNetwork.h"
#include "bsLockFree.h"
#include "cmConst.h"
#include "cmRecord.h"

class cmInterface;

#define MAX_REMOTE_COMMAND_BYTE_SIZE (32*1024)

class cmCnx {
public:
    cmCnx(cmInterface* itf, int port);
    ~cmCnx(void);

    void runRxFromClient(void);
    void runTxToClient(void);
    void injectFiles(const bsVec<bsString>& filenames);

    bsVec<u8>* getTxBuffer (int streamId);
    void       sendTxBuffer(int streamId);

private:
    cmCnx(const cmCnx& other); // To please static analyzers
    cmCnx& operator=(cmCnx other);

    // Private methods
    bool checkConnection(const bsVec<bsString>& importedFilenames, bsSocket_t sockfd);
    int  initializeTransport(FILE* fd, bsSocket_t socketd, bsString& errorMsg);
    void dataReceptionLoop  (bsSocket_t masterSockFd);
    bool parseTransportLayer(int streamId, u8* buf, int qty);
    bool processNewEvents   (int streamId, u8* buf, int eventQty);

    static constexpr int CLIENT_HEADER_SIZE = 8;
    struct ParsingCtx {
        // Parsing
        int headerLeft;
        int stringLeft;
        int eventLeft;
        int eventHeaderLeft;
        int remoteLeft;
        bsVec<u8> tempStorage;
        bool isCollectionTick = false;
        void reset(void) {
            headerLeft = CLIENT_HEADER_SIZE;
            stringLeft = eventLeft = eventHeaderLeft = remoteLeft = 0;
            tempStorage.clear();
            isCollectionTick = false;
        }
    };
    struct StreamInfo {
        cmStreamInfo infos;
        // Rx
        s64    timeOriginTick;
        s64    syncDateTick;
        double tickToNs;
        ParsingCtx parsing;
        FILE*      fileDescr   = 0;
        bsSocket_t socketDescr = (bsSocket_t)bsSocketError;
        // Tx
        bsVec<u8>        txBuffer;
        std::atomic<int> txBufferState; // 0=free  1=under fill   2=sending
        // Helpers
        void reset(void) {
            parsing.reset();
            txBufferState.store(0);
            if(fileDescr!=0) { fclose(fileDescr); fileDescr = 0; }
            if(socketDescr!=bsSocketError) { bsOsCloseSocket(socketDescr); socketDescr = (bsSocket_t)bsSocketError; }
        }
    };

    // Both direction
    cmInterface*     _itf;
    int              _port;
    std::atomic<int> _doStopThreads;
    bsSocket_t       _clientSocket[cmConst::MAX_STREAM_QTY];
    bool             _rxIsStarted = false;
    bool             _txIsStarted = false;
    bsVec<u8>        _conversionBuffer;
    std::mutex       _threadInitMx;
    std::condition_variable _threadInitCv;
    bsMsgExchanger<bsVec<bsString>> _msgInjectFile;
    bool             _isSocketInput;  // True: socket                  False: import from file
    bool             _isMultiStream;  // True: multi stream accepted   False: only one stream
    // Reception
    static constexpr int _recBufferSize = 256000;
    u8*          _recBuffer = 0;
    std::thread* _threadClientRx = 0;
    bsUs_t       _lastDeltaRecordTime = 0;
    s64          _timeOriginTick;
    u64          _timeOriginCoarseNs;
    double       _tickToNs;

    // Extracted characteristics
    bool       _recordToggleBytes  = false;
    int        _streamQty;
    StreamInfo _streams[cmConst::MAX_STREAM_QTY];

    // Transmission
    std::thread* _threadClientTx = 0;
    std::mutex   _threadWakeUpMx;
    std::condition_variable _threadWakeUpCv;
};
