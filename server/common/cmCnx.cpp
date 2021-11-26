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

// This file implements the socket connection to a program and its protocol, in a dedicated thread.

// System
#include <cstring>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <iterator>

// Internal
#include "bsOs.h"
#include "bsVec.h"
#include "cmCnx.h"
#include "cmConst.h"
#include "cmCompress.h"

#ifndef PL_GROUP_CLIENTRX
#define PL_GROUP_CLIENTRX 1
#endif

#ifndef PL_GROUP_CLIENTTX
#define PL_GROUP_CLIENTTX 1
#endif


constexpr int SUPPORTED_MIN_PROTOCOL = 2;
constexpr int SUPPORTED_MAX_PROTOCOL = 2;

cmCnx::cmCnx(cmInterface* itf, int port) :
    _itf(itf), _port(port), _doStopThreads(0)
{
#if defined(_WIN32)
    // Windows special case: initialize the socket library
    WSADATA wsaData;
    int wsaInitStatus = WSAStartup(MAKEWORD(2,2), &wsaData);
    plAssert(wsaInitStatus==0, "Unable to initialize winsock", wsaInitStatus);
#endif

    // Initialize the streams
    for(int i=0; i<cmConst::MAX_STREAM_QTY; ++i) {
        _streams[i].reset();
        _streams[i].parsing.tempStorage.reserve(256);
        _streams[i].txBuffer.reserve(MAX_REMOTE_COMMAND_BYTE_SIZE);
    }

    plAssert(_recBufferSize>=256);
    _recBuffer = new u8[_recBufferSize];
    plAssert(_recBuffer);

    // Launch the threads
    _threadClientTx = new std::thread([this]{ this->runTxToClient(); });
    plAssert(_threadClientTx);
    _threadClientRx = new std::thread([this]{ this->runRxFromClient(); });
    plAssert(_threadClientRx);

    // Wait for both threads readiness
    std::unique_lock<std::mutex> lk(_threadInitMx);
    _threadInitCv.wait(lk, [this] { return _txIsStarted && _rxIsStarted; });
}


cmCnx::~cmCnx(void)
{
    _doStopThreads.store(1);
    _threadWakeUpCv.notify_one(); // Breaks the client tx loop
    _threadClientTx->join();
    _threadClientRx->join();
    delete   _threadClientTx;
    delete   _threadClientRx;
    delete[] _recBuffer;
#if defined(_WIN32)
    WSACleanup();
#endif
}


void
cmCnx::injectFiles(const bsVec<bsString>& filenames)
{
    bsVec<bsString>* s = _msgInjectFile.t1GetFreeMsg();
    if(!s) return;
    *s = filenames;
    _msgInjectFile.t1Send();
}


// ==============================================================================================
// Transmission to client
// ==============================================================================================

bsVec<u8>*
cmCnx::getTxBuffer(int streamId)
{
    int zero = 0;  // Due to CAS API
    StreamInfo& si = _streams[streamId];
    return si.txBufferState.compare_exchange_strong(zero, 1)? &si.txBuffer : 0;
 }


void
cmCnx::sendTxBuffer(int streamId)
{
    StreamInfo& si = _streams[streamId];
    si.txBufferState.store(2);
    _threadWakeUpCv.notify_one();
}


void
cmCnx::runTxToClient(void)
{
    plDeclareThread("Client Tx");
    plgText(CLIENTTX, "State", "Start of client transmission thread");

    {
        // Notify the initialization that transmission thread is ready
        std::lock_guard<std::mutex> lk(_threadInitMx);
        _txIsStarted = true;
        _threadInitCv.notify_one();
    }

    // Loop until end of program
    while(!_doStopThreads.load()) {

        // Wait for a buffer to be sent, among all streams
        std::unique_lock<std::mutex> lk(_threadWakeUpMx);
        _threadWakeUpCv.wait(lk, [this] {
            if(_doStopThreads.load()) return true;
            for(int streamId=0; streamId<cmConst::MAX_STREAM_QTY; ++streamId) {
                if(_streams[streamId].txBufferState.load()==2) return true;
            }
            return false;
        });
        if(_doStopThreads.load()) break; // End of program

        for(int streamId=0; streamId<cmConst::MAX_STREAM_QTY; ++streamId) {
            StreamInfo& si = _streams[streamId];
            if(si.txBufferState.load()!=2) continue;

            // Is there a connection?
            if(si.socketDescr==bsSocketError) {
                si.txBufferState.store(0); // Free for next command to send
                _itf->notifyCommandAnswer(streamId, plPriv::plRemoteStatus::PL_ERROR, "No socket");
                continue;
            }

            // Is control enabled? (else no need to send because the app will not listen)
            if(si.infos.tlvs[PL_TLV_HAS_NO_CONTROL]) {
                si.txBufferState.store(0); // Free for next command to send
                _itf->notifyCommandAnswer(streamId, plPriv::plRemoteStatus::PL_ERROR, "Control is disabled on application side");
                continue;
            }

            // Sending. Loop on sending call until fully sent
            int offset = 0;
            while(offset<si.txBuffer.size()) {
#ifdef _WIN32
                int qty = send(si.socketDescr, (const char*)&si.txBuffer[offset], si.txBuffer.size()-offset, 0);
#else
                int qty = send(si.socketDescr, (const char*)&si.txBuffer[offset], si.txBuffer.size()-offset, MSG_NOSIGNAL);  // MSG_NOSIGNAL to prevent sending SIGPIPE
#endif
                if(qty<=0) {
                    _itf->notifyCommandAnswer(streamId, plPriv::plRemoteStatus::PL_ERROR, "Bad socket sending");
                    break;
                }
                offset += qty;
            }
            si.txBufferState.store(0); // Free the request buffer for the next one
        } // End of loop on streams

    } // End of connection loop

    plgText(CLIENTTX, "State", "End of data reception thread");
}


// ==============================================================================================
// Reception from client
// ==============================================================================================

bool
cmCnx::checkConnection(const bsVec<bsString>& importedFilenames, bsSocket_t masterSockFd)
{
    // Reset the connection automata
    _isMultiStream = _itf->isMultiStreamEnabled();
    _streamQty = 0;
    for(int sid=0; sid<cmConst::MAX_STREAM_QTY; ++sid) _streams[sid].reset();

    bool isInitValid = true;
    bsString errorMsg;

    // If the record processing pipeline is not available, just close the socket
    if(!_itf->isRecordProcessingAvailable()) return false;

    // Imported file case
    // ==================
    if(!importedFilenames.empty()) {
        _isSocketInput = false;

        // Loop on files
        for(const bsString& filename : importedFilenames) {

            // Open the filename
            _itf->log(LOG_INFO, "Open file %s for import", filename.toChar());
            FILE* fd = fopen(filename.toChar(), "rb");
            if(!fd) {
                isInitValid = false;
                errorMsg = bsString("Unable to open the file: ")+filename;
                break;
            }

            // Retrieve the stream initialization information and check them
            int streamId = initializeTransport(fd, (bsSocket_t)bsSocketError, errorMsg);
            isInitValid  = (streamId>=0);

            // Store in stream order (unless there is an error)
            if(streamId>=0) {
                StreamInfo& si = _streams[streamId];
                plAssert(si.fileDescr==0); // By design of the transport initialization, else an error would be raised
                si.fileDescr = fd;
            }
            else fclose(fd);

            if(!isInitValid) break;
        }

    } // End of imported file case


    // Socket case
    // ==========
    else {
        _isSocketInput = true;

        // Wait for a client program to connect
        int connectionQty = 0;
        while(true) {  // Loop is ended when no new connection found

            // First connection has a longer timeout than the potential next ones, so that we do not busy loop
            //   when waiting for a new record. The next connections shall be ~immediate, or delayed to the
            //   data reception part
            timeval tv;
            tv.tv_sec  = 0;  // Select may update the timeout, so we need to set it back
            tv.tv_usec = connectionQty? 10000 : 100000;
            // Observe only the master socket. Nneed to be redone at each loop as 'listen' calls are destructive
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(masterSockFd, &fds);

            // Check for new connection
            int selectRet = select(masterSockFd+1, &fds, NULL, NULL, &tv);
            if(selectRet==-1) {
                _itf->log(LOG_WARNING, "Client reception: failed to check for activity on the sockets");
                isInitValid = false;
                break;
            }

            // No activity
            if(selectRet==0) {
                // Case no connection at all: go back to the main task waiting loop
                if(connectionQty==0) return false;
                // If already one connection is there, then we can start the new recording.
                // Other potential streams will be accepted during the data reception
                break;
            }

            // Case a new connection occurred (=activity on master socket)
            if(FD_ISSET(masterSockFd, &fds)) {

                struct sockaddr_in clientAddress;
                socklen_t sosize  = sizeof(clientAddress);
                bsSocket_t sock = (bsSocket_t)accept(masterSockFd, (sockaddr*)&clientAddress, &sosize);
                if(!bsIsSocketValid(sock)) break;

                // Retrieve the stream initialization information and check it
                int streamId = -1;
                if(connectionQty==0 || _isMultiStream) {
                    streamId = initializeTransport(0, sock, errorMsg);
                    isInitValid  = (streamId>=0);
                }
                else _itf->log(LOG_WARNING, "Client reception in monostream mode: ignoring incoming socket");

                // Store in stream order (unless there is an error)
                if(streamId>=0) {
                    StreamInfo& si = _streams[streamId];
                    plAssert(si.socketDescr==bsSocketError); // By design of the transport initialization, else an error would be raised
                    si.socketDescr = sock;
                    ++connectionQty;
                }
                else bsOsCloseSocket(sock);

                if(!isInitValid) break;
            }

        } // End of loop to get all socket connections

    } // End of socket case


    // Finalize the initial setup: find the start date
    // ===============================================

    if(isInitValid) {
        // Find the clock characteristics. "Complex" only in the multistream case
        _tickToNs       = _streams[0].tickToNs; // All clocks should be synchronized externally
        _timeOriginTick = _streams[0].timeOriginTick;

        if(_streams[0].infos.tlvs[PL_TLV_HAS_SHORT_DATE]==0) {
            // For "long" date, the origin is simply the earliest origin of all streams
            for(int sid=1; sid<_streamQty; ++sid) {
                if(_streams[sid].timeOriginTick<_timeOriginTick) _timeOriginTick = _streams[sid].timeOriginTick;
            }
        }
        else {
            // Short date case
            // Find the earliest global clock date, corrected by the high resolution timestamp
            int earliestSid = 0;
            for(int sid=1; sid<_streamQty; ++sid) {
                const cmStreamInfo& t  = _streams[sid].infos;
                const cmStreamInfo& te = _streams[earliestSid].infos;
                // First, compare the global clock
                // If equal, compare the high resolution timestamps taking the wrap into account
                if(t.tlvs[PL_TLV_HAS_SHORT_DATE]<te.tlvs[PL_TLV_HAS_SHORT_DATE] ||
                   (t.tlvs[PL_TLV_HAS_SHORT_DATE]==te.tlvs[PL_TLV_HAS_SHORT_DATE] &&
                    (((u32)((u32)_streams[sid].timeOriginTick-(u32)_streams[earliestSid].timeOriginTick))&0x80000000))) {
                    earliestSid = sid;
                }
            }

            // Store the origin of the record (zero wrap)
            _timeOriginTick     = _streams[earliestSid].timeOriginTick;
            _timeOriginCoarseNs = _streams[earliestSid].infos.tlvs[PL_TLV_HAS_SHORT_DATE];

            // Compute the initial wrap count for all streams
            s64 wrapPeriodNs = (s64)(_tickToNs*(double)(1LL<<32));
            for(int sid=0; sid<_streamQty; ++sid) {
                StreamInfo& si     = _streams[sid];
                u64 timeCoarseNs   = si.infos.tlvs[PL_TLV_HAS_SHORT_DATE];
                s64 wrapQty        = (timeCoarseNs-_timeOriginCoarseNs)/wrapPeriodNs;
                si.timeOriginTick |= (wrapQty<<32);
                if(_timeOriginCoarseNs+(u64)(_tickToNs*si.timeOriginTick)<timeCoarseNs-wrapPeriodNs/2) {
                    si.timeOriginTick += (1LL<<32);
                }
            }

        }
        plAssert(_timeOriginTick>=0);
    }

    else {
        // Case of error: clean resources
        _itf->notifyErrorForDisplay(_streams[0].fileDescr==0? ERROR_GENERIC : ERROR_IMPORT, errorMsg.toChar());
        for(int sid=0; sid<cmConst::MAX_STREAM_QTY; ++sid) {
            StreamInfo& si = _streams[sid];
            if(si.fileDescr!=0)               { fclose(si.fileDescr); si.fileDescr = 0; }
            if(si.socketDescr!=bsSocketError) { bsOsCloseSocket(si.socketDescr); si.socketDescr = (bsSocket_t)bsSocketError; }
        }
    }

    return isInitValid;
}


void
cmCnx::dataReceptionLoop(bsSocket_t masterSockFd)
{
    // Notify the start of the recording
    if(!_itf->notifyRecordStarted(_streams[0].infos, _timeOriginTick, _tickToNs)) {
        // Error message is already handled inside main
        for(int sid=0; sid<cmConst::MAX_STREAM_QTY; ++sid) {
            StreamInfo& si = _streams[sid];
            if(si.fileDescr!=0)               { fclose(si.fileDescr); si.fileDescr = 0; }
            if(si.socketDescr!=bsSocketError) { bsOsCloseSocket(si.socketDescr); si.socketDescr = (bsSocket_t)bsSocketError; }
        }
        return;
    }

    // Notify the other initial streams (the first one is already declared)
    for(int streamId=1; streamId<_streamQty; ++streamId) {
        _itf->notifyNewStream(_streams[streamId].infos);
    }

    int  deltaRecordFactor = _isSocketInput? 1 : 5; // No need to have "real time" display when we import from file
    bool isRecordOk         = true;
    bool areNewDataReceived = false;
    int  streamId           = 0;
    timeval tv;
    _lastDeltaRecordTime = bsGetClockUs();

    plgText(CLIENTRX, "State", "Start data reception");
    while(!_doStopThreads.load()) {

        // Manage the periodic live display update
        bsUs_t currentTime = bsGetClockUs();
        if(areNewDataReceived && (currentTime-_lastDeltaRecordTime)>=deltaRecordFactor*cmConst::DELTARECORD_PERIOD_US) {
            if(_itf->createDeltaRecord()) {
                _lastDeltaRecordTime = currentTime;
            }
            areNewDataReceived = false;
        }

        // Read the data
        int qty;
        if(!_isSocketInput) {
            // Read the file streams one after the other @#LATER The next stream could be computed a the "most late" one, for a better live display
            plAssert(_streams[streamId].fileDescr);
            qty = (int)fread(_recBuffer, 1, _recBufferSize, _streams[streamId].fileDescr);
            if(qty<=0) ++streamId;  // Go to next file
            if(streamId>=_streamQty) break;  // Import file after file is complete

            // Parse the received content
            areNewDataReceived = parseTransportLayer(streamId, _recBuffer, qty);
            if(!areNewDataReceived) {
                _itf->log(LOG_ERROR, "Client reception: Error in parsing the received data");
                plgText(CLIENTRX, "State", "Error in parsing the received data");
                isRecordOk = false; // Corrupted record
                break;
            }
        }

        else {
            // Check the socket activity
            tv.tv_sec  = 0;     // 'select' call may modify the timeout, so we need to set it back
            tv.tv_usec = 10000;
            fd_set fds;
            FD_ZERO(&fds);
            bsSocket_t maxFd = masterSockFd;
            FD_SET(masterSockFd, &fds);  // To detect new connections
            bool hasValidStream = false;
            for(streamId=0; streamId<_streamQty; ++streamId) {
                StreamInfo& si = _streams[streamId];
                if(si.socketDescr!=bsSocketError) {
                    FD_SET(si.socketDescr, &fds);
                    if(si.socketDescr>maxFd) maxFd = si.socketDescr;
                    hasValidStream = true;
                }
            }
            if(!hasValidStream) break; // No more valid connection
            int selectRet = select(maxFd+1, &fds, NULL, NULL, &tv);  // The 10 ms timeout prevents busy looping
            if(selectRet==-1) break; // Socket issue

            if(selectRet>0) {

                // Get the buffers from each active socket, one after the other (buffer fairness)
                for(streamId=0; streamId<_streamQty; ++streamId) {
                    StreamInfo& si = _streams[streamId];
                    if(!FD_ISSET(si.socketDescr, &fds)) continue;

#ifdef _WIN32
                    qty = recv(si.socketDescr, (char*)_recBuffer, _recBufferSize, 0);
#else
                    qty = recv(si.socketDescr, (char*)_recBuffer, _recBufferSize, MSG_DONTWAIT);
#endif
                    if((bsGetSocketError()==EAGAIN || bsGetSocketError()==EWOULDBLOCK) && qty<0) continue;  // Timeout on reception (empty)

                    // Client is disconnected?
                    if(qty<1) {
                        bsOsCloseSocket(si.socketDescr);
                        si.socketDescr = (bsSocket_t)bsSocketError;
                        continue;
                    }

                    // Parse the received content
                    areNewDataReceived = parseTransportLayer(streamId, _recBuffer, qty);
                    if(!areNewDataReceived) {
                        _itf->log(LOG_ERROR, "Client reception: Error in parsing the received data");
                        plgText(CLIENTRX, "State", "Error in parsing the received data");
                        isRecordOk = false; // Corrupted record
                        break;
                    }
                }  // Loop on streams

                // New incoming multi-stream connection?
                if(FD_ISSET(masterSockFd, &fds)) {

                    struct sockaddr_in clientAddress;
                    socklen_t sosize = sizeof(clientAddress);
                    bsSocket_t sock  = (bsSocket_t)accept(masterSockFd, (sockaddr*)&clientAddress, &sosize);
                    if(!bsIsSocketValid(sock)) break;

                    bsString errorMsg;
                    streamId = -1;
                    if(_isMultiStream) {
                        streamId = initializeTransport(0, sock, errorMsg);
                    }
                    else _itf->log(LOG_WARNING, "Client reception in monostream mode: ignoring incoming socket");

                    if(streamId>=0) {
                        StreamInfo& si = _streams[streamId];
                        plAssert(si.socketDescr==bsSocketError); // By design of the transport initialization, else an error would be raised
                        si.socketDescr = sock;

                        // Update the stream's start date in case of short date
                        if(_streams[0].infos.tlvs[PL_TLV_HAS_SHORT_DATE]!=0) {
                            s64 wrapPeriodNs   = (s64)(_tickToNs*(double)(1LL<<32));
                            u64 timeCoarseNs   = si.infos.tlvs[PL_TLV_HAS_SHORT_DATE];
                            s64 wrapQty        = (timeCoarseNs-_timeOriginCoarseNs)/wrapPeriodNs;
                            si.timeOriginTick |= (wrapQty<<32);
                            if(_timeOriginCoarseNs+(u64)(_tickToNs*si.timeOriginTick)<timeCoarseNs-wrapPeriodNs/2) {
                                si.timeOriginTick += (1LL<<32);
                            }
                        }

                        // Notify the server side
                        _itf->notifyNewStream(si.infos);
                    }
                    else bsOsCloseSocket(sock);
                }

            }  // Global socket activity detected
        }  // Socket case


    } // End of reception loop

    plgText(CLIENTRX, "State", "End of data reception");
    _itf->notifyRecordEnded(isRecordOk);

    for(int sid=0; sid<cmConst::MAX_STREAM_QTY; ++sid) {
        StreamInfo& si = _streams[sid];
        if(si.fileDescr!=0)               { fclose(si.fileDescr); si.fileDescr = 0; }
        if(si.socketDescr!=bsSocketError) { bsOsCloseSocket(si.socketDescr); si.socketDescr = (bsSocket_t)bsSocketError; }
    }
    if(_isSocketInput) {
        _itf->log(LOG_DETAIL, "Client reception: Closed client connection");
    }
    plgText(CLIENTRX, "State", "End of recording");
}


void
cmCnx::runRxFromClient(void)
{
    plDeclareThread("Client Rx");
    plgText(CLIENTRX, "State", "Start of client reception thread");

    // Creation of the TCP connection
    plgBegin(CLIENTRX, "Create the listening socket");
    bsSocket_t masterSockFd = (bsSocket_t)socket(AF_INET,SOCK_STREAM,0);
    if(!bsIsSocketValid(masterSockFd)) {
        _itf->log(LOG_ERROR, "Client reception: unable to create a socket");
        plgText(CLIENTRX, "State", "Error, unable to create");
    }

    // set socket for reuse (otherwise might have to wait 4 minutes every time socket is closed)
    int option = 1;
    setsockopt(masterSockFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&option, sizeof(option));

    sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons((u16)_port);

    timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 10000;
    setsockopt(masterSockFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)); // Timeout on reception. Probably do the same later with SO_SNDTIMEO

    _itf->log(LOG_INFO, "Client reception: Binding socket on port %d", _port);
    int bindSuccess = bind(masterSockFd, (sockaddr*)&serverAddress, sizeof(serverAddress));
    if(bindSuccess==-1) {
        _itf->log(LOG_ERROR, "Client reception: unable to bind socket");
        plgText(CLIENTRX, "State", "Error, unable to bind");
    }
    int listenSuccess = listen(masterSockFd, cmConst::MAX_STREAM_QTY); // Accept all streams
    if(listenSuccess==-1) {
        _itf->log(LOG_ERROR, "Client reception: unable to listen to socket");
        plgText(CLIENTRX, "State", "Error, unable to listen");
    }
    plgText(CLIENTRX, "State", "Socket ok");
    plgEnd(CLIENTRX, "Create the listening socket");

    // Notify the initialization that reception thread is ready
    {
        std::lock_guard<std::mutex> lk(_threadInitMx);
        _rxIsStarted = true;
        _threadInitCv.notify_one();
    }

    if(bindSuccess!=-1 && listenSuccess!=-1) {
        _itf->log(LOG_INFO, "Client reception: Start the socket listening loop");
    } else {
        char tmpStr[256];
        snprintf(tmpStr, sizeof(tmpStr), "Unable to listen for program connections. Please check that the port %d is not already in use", _port);
        _itf->notifyErrorForDisplay(ERROR_GENERIC, tmpStr);
    }

    // Connection loop
    bool isWaitingDisplayed = false;
    bsVec<bsString>   importedFilenames;

    plgText(CLIENTRX, "State", "Start the server loop");
    while(!_doStopThreads.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if(!isWaitingDisplayed) {
            isWaitingDisplayed = true;
            plgText(CLIENTRX, "State", "Waiting for a client");
        }

        // Import from file
        bsVec<bsString>* msgInjectedFilenames = _msgInjectFile.getReceivedMsg();
        importedFilenames.clear();
        if(msgInjectedFilenames) {
            importedFilenames = *msgInjectedFilenames;
            _msgInjectFile.releaseMsg();
        }

        // Check connection and loop on received data
        if(checkConnection(importedFilenames, masterSockFd)) {
            isWaitingDisplayed = false;
            dataReceptionLoop(masterSockFd);
        }
    }
    plgText(CLIENTRX, "State", "End of server loop");

    bsOsCloseSocket(masterSockFd);
    plgText(CLIENTRX, "State", "End of data reception thread");
}


int
cmCnx::initializeTransport(FILE* fd, bsSocket_t socketd, bsString& errorMsg)
{
    plgScope(CLIENTRX, "initializeTransport");
    plAssert(bsIsSocketValid(socketd)!=(fd!=0)); // Exclusive input
    // We wait for the following informations
    //  - 8B: Magic (8 bytes). Check the connection type
    //  - 4B: 0x12345678 sent as a u32, in order to detect the endianness (required for data reception)
    //  - 4B: TLV total size. So first reception shall be 16 bytes, then second reception is the TLV total size
    //  - then TLVs, with the structure: type 2B, length 2B, length padded to 4B alignment (all in big endian)
    //  - Shall include the TLVs "protocol number" (first), "clock info" (origin + event tick to nanosecond) and the "application name" (zero terminated)
    //    Other TLVs are optional

    char tmpStr[256];
    bsVec<u8> header; header.reserve(256);
    s64    timeOriginTick = 0;
    double tickToNs = 0.;
    cmStreamInfo si;
    memset(si.tlvs, 0, sizeof(si.tlvs));

#define STREAM_ERROR(cond_, msg_)                                       \
    if(cond_) {                                                         \
        snprintf(tmpStr, sizeof(tmpStr), "Error for stream #%d: " msg_, _streamQty); \
        errorMsg = tmpStr;                                              \
        return -1;                                                      \
}

#define CHECK_TLV_PAYLOAD_SIZE(expectedSize, name)                      \
    if(tlvLength!=(expectedSize)) {                                     \
        errorMsg = "Client send a corrupted " name " TLV";              \
        return -1;                                                      \
    }

    STREAM_ERROR(_streamQty>=cmConst::MAX_STREAM_QTY,
                 "Maximum stream quantity has been reached, refusing this new one.");

    // Read all header bytes
    // =====================

    // Read 16 first bytes
    constexpr int expectedHeaderSize = 16;
    if(fd) {
        header.resize(expectedHeaderSize);
        int qty = (int)fread(&header[0], 1, expectedHeaderSize, fd);
        header.resize(bsMax(qty, 0));
    } else {
        int remainingTries = 50;
        while(remainingTries>0 && !_doStopThreads.load() && header.size()<expectedHeaderSize) {
            int qty = recv(socketd, (char*)_recBuffer, expectedHeaderSize-header.size(), 0);
            if((bsGetSocketError()==EAGAIN || bsGetSocketError()==EWOULDBLOCK) && qty<0) {
                --remainingTries;
                continue; // Timeout on reception (empty)
            }
            else if(qty<1) break; // Client is disconnected
            else std::copy(_recBuffer, _recBuffer+qty, std::back_inserter(header));
        }
    }

    // Analyse the first chapter of the header
    if(header.size()!=expectedHeaderSize) {
        errorMsg = "Client did not send the full connection establishment header.";
        return -1;
    }
    if(strncmp((char*)&header[0], "PL-MAGIC", 8)!=0) {
        errorMsg = "Client sent bad connection magic (probably not a Palanteer client)";
        return -1;
    }
    if(*((u32*)&header[8])!=0x12345678 && *((u32*)&header[8])!=0x78563412) {
        errorMsg = "Client sent unexpected endianness detection string value";
        return -1;
    }
    _recordToggleBytes = (*((u32*)&header[8])==0x78563412);
    int totalTlvLength = (header[12]<<24) | (header[13]<<16) | (header[14]<<8) | header[15];
    if(totalTlvLength>_recBufferSize) {
        errorMsg = "Client sent corrupted header element length";
        return -1;
    }

    // Read the TLVs
    header.clear();
    if(fd) {
        header.resize(totalTlvLength);
        int qty = (int)fread(&header[0], 1, totalTlvLength, fd);
        header.resize(bsMax(qty, 0));
    } else {
        int remainingTries = 3; // 3 second total timeout to get the header
        while(remainingTries>0 && !_doStopThreads.load() && header.size()<totalTlvLength) {
            int qty = recv(socketd, (char*)_recBuffer, totalTlvLength-header.size(), 0);
            if((bsGetSocketError()==EAGAIN || bsGetSocketError()==EWOULDBLOCK) && qty<0) {
                --remainingTries;
                continue; // Timeout on reception (empty)
            }
            else if(qty<1) break; // Client is disconnected
            else std::copy(_recBuffer, _recBuffer+qty, std::back_inserter(header));
        }
    }

    // Parse the received TLVs
    // =======================

    if(header.size()!=totalTlvLength) {
        errorMsg = "Client did not fully send a header element";
        return -1;
    }
    int offset = 0;
    while(offset<=totalTlvLength-4) {
        int tlvType   = (header[offset+0]<<8) | header[offset+1];
        int tlvLength = (header[offset+2]<<8) | header[offset+3];
        if(offset+4+tlvLength>totalTlvLength) {
            errorMsg = "Client send a corrupted header element";
            return -1;
        }

        switch(tlvType) {

        case PL_TLV_PROTOCOL:
            CHECK_TLV_PAYLOAD_SIZE(2, "Protocol");
            si.tlvs[tlvType] = (header[offset+4]<<8) | (header[offset+5]<<0);
            _itf->log(LOG_DETAIL, "   Protocol version is %d", (int)si.tlvs[tlvType]);
            plgData(CLIENTRX, "Protocol version is", si.tlvs[tlvType]);
            break;

        case PL_TLV_CLOCK_INFO: // Clock info TLV: origin and tick to nanosecond coefficient
            CHECK_TLV_PAYLOAD_SIZE(16, "Clock Info");
            timeOriginTick = (((u64)header[offset+ 4]<<56) | ((u64)header[offset+ 5]<<48) |
                              ((u64)header[offset+ 6]<<40) | ((u64)header[offset+ 7]<<32) |
                              ((u64)header[offset+ 8]<<24) | ((u64)header[offset+ 9]<<16) |
                              ((u64)header[offset+10]<< 8) |  (u64)header[offset+11]);
            {
                u64 tmp = (((u64)header[offset+12]<<56) | ((u64)header[offset+13]<<48) |
                           ((u64)header[offset+14]<<40) | ((u64)header[offset+15]<<32) |
                           ((u64)header[offset+16]<<24) | ((u64)header[offset+17]<<16) |
                           ((u64)header[offset+18]<< 8) |  (u64)header[offset+19]);
                char* tmp1 = (char*)&tmp; // Avoids a warning on strict aliasing
                tickToNs  = *(double*)tmp1;
            }
            si.tlvs[tlvType] = (u64)(1000.*tickToNs); // Pico second, as precision can be deep. For display only
            _itf->log(LOG_DETAIL, "   Clock precision is %.1f ns", tickToNs);
            plgData(CLIENTRX, "Time tick origin is", timeOriginTick);
            plgData(CLIENTRX, "Time tick unit is",   tickToNs);
            break;

        case PL_TLV_APP_NAME:
            // Get the application name
            si.appName = bsString((char*)&header[offset+4], (char*)(&header[offset+4]+tlvLength));
            if(!si.appName.empty() && si.appName.back()==0) si.appName.pop_back();
            // Filter out the problematic characters
            {
                int i=0;
                for(u8 c : si.appName) {
                    if(c<0x1F || c==0x7F || c=='"' || c=='*' || c=='/' || c=='\\' ||
                       c==':' || c=='<' || c=='>' || c=='^' || c=='?' || c=='|') continue;
                    si.appName[i++] = c;
                }
                si.appName.resize(i);
                si.appName.strip();
            }
            // Some logging
            _itf->log(LOG_DETAIL, "   Application name is '%s'", si.appName.toChar());
            plgData(CLIENTRX, "Application name", si.appName.toChar());
            break;

        case PL_TLV_HAS_BUILD_NAME:
            // Get the build name
            si.buildName = bsString((char*)&header[offset+4], (char*)(&header[offset+4]+tlvLength));
            if(!si.buildName.empty() && si.buildName.back()==0) si.buildName.pop_back();
            _itf->log(LOG_DETAIL, "   Build name is '%s'", si.buildName.toChar());
            plgData(CLIENTRX, "Build name", si.buildName.toChar());
            break;

        case PL_TLV_HAS_LANG_NAME:
            // Get the language name
            si.langName = bsString((char*)&header[offset+4], (char*)(&header[offset+4]+tlvLength));
            if(!si.langName.empty() && si.langName.back()==0) si.langName.pop_back();
            _itf->log(LOG_DETAIL, "   Language name is '%s'", si.langName.toChar());
            plgData(CLIENTRX, "Language name", si.langName.toChar());
            break;

        case PL_TLV_HAS_EXTERNAL_STRING:
            CHECK_TLV_PAYLOAD_SIZE(0, "External String Flag");
            si.tlvs[tlvType] = 1;
            _itf->log(LOG_DETAIL, "   External string is activated");
            plgText(CLIENTRX, "State", "External String Flag set");
            break;

        case PL_TLV_HAS_SHORT_STRING_HASH:
            CHECK_TLV_PAYLOAD_SIZE(0, "Short String Hash Flag");
            si.tlvs[tlvType] = 1;
            _itf->log(LOG_DETAIL, "   Short string hash is activated");
            plgText(CLIENTRX, "State", "Short String Hash Flag set");
            break;

        case PL_TLV_HAS_NO_CONTROL:
            CHECK_TLV_PAYLOAD_SIZE(0, "No Control Flag");
            si.tlvs[tlvType] = 1;
            _itf->log(LOG_DETAIL, "   Remote control is disabled");
            plgText(CLIENTRX, "State", "No Control Flag set");
            break;

        case PL_TLV_HAS_SHORT_DATE:
            CHECK_TLV_PAYLOAD_SIZE(8, "Short Date Flag");
            // This is the global clock date in ns with precision higher than 1/4 of the wrap period
            si.tlvs[tlvType] = (((u64)header[offset+ 4]<<56) | ((u64)header[offset+ 5]<<48) |
                                    ((u64)header[offset+ 6]<<40) | ((u64)header[offset+ 7]<<32) |
                                    ((u64)header[offset+ 8]<<24) | ((u64)header[offset+ 9]<<16) |
                                    ((u64)header[offset+10]<< 8) |  (u64)header[offset+11]);
            if(si.tlvs[tlvType]==0) si.tlvs[tlvType] = 1; // 0 is reserved for "long date"
            _itf->log(LOG_DETAIL, "   Short date is activated");
            plgText(CLIENTRX, "State", "Short Date Flag set");
            break;

        case PL_TLV_HAS_COMPACT_MODEL:
            CHECK_TLV_PAYLOAD_SIZE(0, "Compact Model Flag");
            si.tlvs[tlvType] = 1;
            _itf->log(LOG_DETAIL, "   Compact model is activated");
            plgText(CLIENTRX, "State", "Compact Model Flag set");
            break;

        case PL_TLV_HAS_HASH_SALT:
            CHECK_TLV_PAYLOAD_SIZE(4, "Hash Salt");
            si.tlvs[tlvType] = (header[offset+4]<<24) | (header[offset+5]<<16) | (header[offset+6]<<8) | (header[offset+7]<<0);
            _itf->log(LOG_DETAIL, "   Hash salt is set to %u", (u32)si.tlvs[tlvType]);
            plgData(CLIENTRX, "Hash salt", si.tlvs[tlvType]);
            break;

        case PL_TLV_HAS_AUTO_INSTRUMENT:
            CHECK_TLV_PAYLOAD_SIZE(0, "Auto Instrument Flag");
            si.tlvs[tlvType] = 1;
            _itf->log(LOG_DETAIL, "   Auto instrumentation is activated");
            plgText(CLIENTRX, "State", "Auto instrumentation Flag set");
            break;

        case PL_TLV_HAS_CSWITCH_INFO:
            CHECK_TLV_PAYLOAD_SIZE(0, "Context Switch Collection Flag");
            si.tlvs[tlvType] = 1;
            _itf->log(LOG_DETAIL, "   Context Switch Collection is activated");
            plgText(CLIENTRX, "State", "Context Switch Collection Flag set");
            break;

        default: // Just ignore unknown TLVs. Protocol compatibility is checked later
            plgData(CLIENTRX, "Skipped unknown TLV", tlvType);
        } // End of switch on TLV type

        // Jump to the next TLV
        offset += 4 + tlvLength;

    } // End of loop on initialization payload

    // Check mandatory information
    STREAM_ERROR(si.appName.empty(), "missing mandatory application name TLV");
    STREAM_ERROR(tickToNs==0., "missing the mandatory clock info TLV");
    STREAM_ERROR(si.tlvs[PL_TLV_PROTOCOL]<SUPPORTED_MIN_PROTOCOL,
                 "the instrumentation library is incompatible (too old) and shall be updated");
    STREAM_ERROR(si.tlvs[PL_TLV_PROTOCOL]>SUPPORTED_MAX_PROTOCOL,
                 "the instrumentation library is incompatible (too recent). The server shall be updated");

    // Check the compatibility versus other streams (these constraints may be removed in the future)
    if(_streamQty>0) {
        STREAM_ERROR(si.tlvs[PL_TLV_HAS_SHORT_STRING_HASH]!=_streams[0].infos.tlvs[PL_TLV_HAS_SHORT_STRING_HASH],
                     "the short string hash flag is inconsistent with other streams");
        STREAM_ERROR((si.tlvs[PL_TLV_HAS_SHORT_DATE]==0)!=(_streams[0].infos.tlvs[PL_TLV_HAS_SHORT_DATE]==0),
                     "the short date flag is inconsistent with other streams");
        STREAM_ERROR(si.tlvs[PL_TLV_HAS_HASH_SALT]!=_streams[0].infos.tlvs[PL_TLV_HAS_HASH_SALT],
                     "the hash salt is inconsistent with other streams");
    }

    // Store the collected infos and return the ID of the accepted new stream
    StreamInfo& storedStream    = _streams[_streamQty++];
    storedStream.timeOriginTick = timeOriginTick;
    storedStream.tickToNs       = tickToNs;
    storedStream.infos          = si;
    plgText(CLIENTRX, "State", "Transport initialized, header is ok");
    _itf->log(LOG_DETAIL, " Stream %d accepted", _streamQty-1);
   return _streamQty-1;
}


bool
cmCnx::processNewEvents(int streamId, u8* buf, int eventQty)
{
    // Direct notification
    if(!_streams[streamId].infos.tlvs[PL_TLV_HAS_COMPACT_MODEL]) {
        return _itf->notifyNewEvents(streamId, (plPriv::EventExt*)buf, eventQty, _streams[streamId].syncDateTick);
    }

    // Compact model: conversion required
    static_assert(sizeof(plPriv::EventExtCompact)==12, "Bad size of compact exchange event structure");
    static_assert(sizeof(plPriv::EventExt       )==24, "Bad size of exchange event structure");
    _conversionBuffer.resize(eventQty*(int)sizeof(plPriv::EventExt));

    plPriv::EventExtCompact* src = (plPriv::EventExtCompact*)buf;
    plPriv::EventExt*        dst = (plPriv::EventExt*)&_conversionBuffer[0];
    for(int i=0; i<eventQty; ++i, ++src, ++dst) {
        dst->threadId = src->threadId;
        dst->flags    = src->flags;
        dst->lineNbr  = src->lineNbr;
        dst->filenameIdx = src->filenameIdx;
        int eType = src->flags&PL_FLAG_TYPE_MASK;
        if(eType!=PL_FLAG_TYPE_ALLOC_PART && eType!=PL_FLAG_TYPE_DEALLOC_PART) {
            dst->nameIdx  = src->nameIdx;
            if(eType==PL_FLAG_TYPE_CSWITCH) {
                if     (dst->nameIdx==0xFFFF) dst->nameIdx = 0xFFFFFFFF;
                else if(dst->nameIdx==0xFFFE) dst->nameIdx = 0xFFFFFFFE;
            }
        } else {
            dst->memSize = src->memSize;
        }
        dst->vU64 = src->vU32;
    }

    // Notify with the converted buffer
    // Having the processing always with the "large" event structure simplifies a lot the code
    return _itf->notifyNewEvents(streamId, (plPriv::EventExt*)&_conversionBuffer[0], eventQty, _streams[streamId].syncDateTick);
}


bool
cmCnx::parseTransportLayer(int streamId, u8* buf, int qty)
{
    // Readability concerns
    ParsingCtx& pc   = _streams[streamId].parsing;
    bsVec<u8>&  s    = pc.tempStorage;
    int eventExtSize = _streams[streamId].infos.tlvs[PL_TLV_HAS_COMPACT_MODEL]? sizeof(plPriv::EventExtCompact) : sizeof(plPriv::EventExtFull);

    // Loop on the buffer content
    while(qty>0) {

        // Header
        if(qty>0 && pc.headerLeft>0) {
            plAssert(pc.stringLeft==0 && pc.eventLeft==0 && pc.eventHeaderLeft==0 && pc.remoteLeft==0,
                     pc.stringLeft, pc.eventLeft, pc.eventHeaderLeft, pc.remoteLeft);

            // Read
            int usedQty = (pc.headerLeft<qty)? pc.headerLeft : qty;
            std::copy(buf, buf+usedQty, std::back_inserter(s));
            buf += usedQty;
            qty -= usedQty;
            pc.headerLeft -= usedQty;

            // Parse if header is complete
            if(pc.headerLeft==0) {
                // Magic
                plAssert(s.size()==CLIENT_HEADER_SIZE);
                if(s[0]!='P' || s[1]!='L') {
                    _itf->log(LOG_ERROR, "Received buffer has a corrupted header");
                    return false;
                }
                // Check the type
                plPriv::DataType dataType = (plPriv::DataType) ((s[2]<<8) | s[3]);
                if(dataType==plPriv::PL_DATA_TYPE_STRING) {
                    pc.stringLeft = (s[4]<<24) | (s[5]<<16) | (s[6]<<8) | s[7];
                }
                else if(dataType==plPriv::PL_DATA_TYPE_EVENT || dataType==plPriv::PL_DATA_TYPE_EVENT_AUX) {
                    pc.eventLeft        = (s[4]<<24) | (s[5]<<16) | (s[6]<<8) | s[7];
                    pc.eventHeaderLeft  = 1;
                    pc.isCollectionTick = (dataType==plPriv::PL_DATA_TYPE_EVENT); // Notification of a collection "tick" for scripting module
                }
                else if(dataType==plPriv::PL_DATA_TYPE_CONTROL) {
                    pc.remoteLeft = (s[4]<<24) | (s[5]<<16) | (s[6]<<8) | s[7];
                }
                else {
                    _itf->log(LOG_WARNING, "Client sent unknown TLV %d - ignored", (int)dataType);
                }
                s.clear();
            }
        } // End of header parsing

        // Strings
        while(qty>0 && pc.stringLeft>0) {
            plAssert(pc.headerLeft==0);
            // Fill the hash
            int usedQty = 0;
            while(usedQty<qty && s.size()<8) s.push_back(buf[usedQty++]);
            buf += usedQty;
            qty -= usedQty;
            // If the 8 bytes hash is complete
            if(s.size()>=8) {
                // Read the string content (that we add after the hash in the buffer)
                usedQty = 0;
                while(usedQty<qty && buf[usedQty]) ++usedQty;
                if(usedQty<qty) ++usedQty; // Consume the trailing zero
                std::copy(buf, buf+usedQty, std::back_inserter(s));
                buf += usedQty;
                qty -= usedQty;
                plAssert(!s.empty());
                if(s.back()==0) {
                    pc.stringLeft -= 1;
                    u64 h = (((u64)s[0])<<56) | (((u64)s[1])<<48) | (((u64)s[2])<<40) | (((u64)s[3])<<32) |
                        (((u64)s[4])<<24) | (((u64)s[5])<<16) | (((u64)s[6])<<8) | (((u64)s[7])<<0);
                    _itf->notifyNewString(streamId, bsString((char*)s.begin()+8, (char*)s.end()), h); // Store the string and the hash (first 8 bytes)
                    s.clear();
                }
            }
        } // End of string parsing

        // Event header
        while(qty>0 && pc.eventHeaderLeft>0) {
            plAssert(pc.headerLeft==0);
            int usedQty = 0;
            while(usedQty<qty && s.size()<8) s.push_back(buf[usedQty++]);
            buf += usedQty;
            qty -= usedQty;
            // If the 8 bytes header is complete, parse the synchronization date (used only for short dates)
            if(s.size()>=8) {
                pc.eventHeaderLeft = 0;
                _streams[streamId].syncDateTick = (((u64)s[0])<<56) | (((u64)s[1])<<48) | (((u64)s[2])<<40) | (((u64)s[3])<<32) |
                    (((u64)s[4])<<24) | (((u64)s[5])<<16) | (((u64)s[6])<<8) | (((u64)s[7])<<0);
                _streams[streamId].syncDateTick += _streams[streamId].timeOriginTick&(~0xFFFFFFFFLL); // Add the origin wrap bias
                s.clear();
            }
        } // End of event header parsing

        // Events
        while(qty>0 && pc.eventHeaderLeft==0 && pc.eventLeft>0) {
            plAssert(pc.headerLeft==0);
            if(!s.empty()) {
                int usedQty = (qty>eventExtSize-s.size())? eventExtSize-s.size() : qty;
                std::copy(buf, buf+usedQty, std::back_inserter(s));
                buf += usedQty;
                qty -= usedQty;
                if(s.size()==eventExtSize) {
                    pc.eventLeft -= 1;
                    if(!processNewEvents(streamId, &s[0], 1)) return false; // Event corruption
                    s.clear();
                }
            }
            int eventQty = qty/eventExtSize;
            if(eventQty>pc.eventLeft) eventQty = pc.eventLeft;
            if(eventQty) {
                if(!processNewEvents(streamId, buf, eventQty)) return false; // Event corruption
                buf += eventQty*eventExtSize;
                qty -= eventQty*eventExtSize;
                pc.eventLeft -= eventQty;
            }
            if(qty>0 && pc.eventLeft>0) {
                plAssert(qty<eventExtSize);
                std::copy(buf, buf+qty, std::back_inserter(s));
                buf += qty;
                qty -= qty;
            }
        } // End of event parsing

        if(pc.isCollectionTick && pc.eventLeft==0) {
            // Notify a collection tick, once the buffer is fully parsed
            pc.isCollectionTick = false;
            _itf->notifyNewCollectionTick(streamId);
        }

        // Remote control
        while(qty>0 && pc.remoteLeft>0) {
            plAssert(pc.headerLeft==0);
            int usedQty = (qty>pc.remoteLeft)? pc.remoteLeft : qty;
            std::copy(buf, buf+usedQty, std::back_inserter(s));
            buf += usedQty;
            qty -= usedQty;
            pc.remoteLeft -= usedQty;
            if(pc.remoteLeft==0) {
                _itf->notifyNewRemoteBuffer(streamId, s);
                s.clear();
            }
        } // End of remote control parsing

        if(pc.headerLeft==0 && pc.stringLeft==0 && pc.eventLeft==0 && pc.eventHeaderLeft==0 && pc.remoteLeft==0) {
            plAssert(s.empty());
            pc.reset();
        }
    } // End of loop on buffer content

    return true;
}
