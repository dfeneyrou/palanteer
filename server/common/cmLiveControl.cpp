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

// This file implements the commands sent to the instrumented program


// Internal
#include "cmLiveControl.h"
#include "cmCnx.h"
#include "cmInterface.h"


cmLiveControl::cmLiveControl(cmInterface* itf, cmCnx* clientCnx) :
    _itf(itf), _clientCnx(clientCnx)
{
}


cmLiveControl::~cmLiveControl(void)
{
}


void
cmLiveControl::storeNewRemoteBuffer(int streamId, bsVec<u8>& buffer)
{
    // 'buffer' contains the command payload (inside the transport layer)
    // Get command type
    plAssert(buffer.size()>=2);
    plPriv::RemoteCommandType ct = (plPriv::RemoteCommandType)((buffer[0]<<8) | buffer[1]);

    plPriv::plRemoteStatus status;
    switch(ct) {
    case plPriv::PL_CMD_STEP_CONTINUE:
    case plPriv::PL_CMD_KILL_PROGRAM:
    case plPriv::PL_CMD_SET_MAX_LATENCY:
    case plPriv::PL_CMD_SET_FREEZE_MODE:
        plAssert(buffer.size()>=4);
        status = (plPriv::plRemoteStatus)((buffer[2]<<8) | buffer[3]);
        _itf->notifyCommandAnswer(streamId, status, "");
        break;
    case plPriv::PL_NTF_FROZEN_THREAD:
        {
            plAssert(buffer.size()>=10);
            u64 bitmap = ((u64)buffer[2]<<56) | ((u64)buffer[3]<<48) | ((u64)buffer[4]<<40) | ((u64)buffer[5]<<32) |
                ((u64)buffer[6]<<24) | ((u64)buffer[7]<<16) | ((u64)buffer[8]<<8) | ((u64)buffer[9]<<0);
            _itf->notifyNewFrozenThreadState(streamId, bitmap);
        }
        break;
    case plPriv::PL_NTF_DECLARE_CLI:
        {
            int cliQty = (buffer[2]<<8) | buffer[3];
            for(int i=0; i<cliQty; ++i) {
                // The 3 strings are the CLI name, param specification and description
                int o = 4+i*6;
                _itf->notifyNewCli(streamId, (buffer[o+0]<<8) | buffer[o+1], (buffer[o+2]<<8) | buffer[o+3], (buffer[o+4]<<8) | buffer[o+5]);
           }
            break;
        }
    case plPriv::PL_CMD_CALL_CLI:
        {
            plAssert(buffer.size()>=7);
            int responseQty = (buffer[2]<<8) | buffer[3];
            int offset = 4;
            plAssert(responseQty==1); // @TEMP Need python ITF redesign to support multiple CLI
            for(int responseNbr=0; responseNbr<responseQty; ++responseNbr) {
                plPriv::plRemoteStatus cliStatus = (plPriv::plRemoteStatus) ((buffer[offset]<<8) | buffer[offset+1]);
                offset += 2;
                int offsetEnd = offset;
                while(offsetEnd<buffer.size() && buffer[offsetEnd]!=0) ++offsetEnd;
                _itf->notifyCommandAnswer(streamId, cliStatus, (char*)&buffer[offset]); // @FIXME Cannot be called N times due to the underlying automata
                offset = offsetEnd+1; // Skip the zero termination
            }
        }
        break;
    }

}


bsVec<u8>*
cmLiveControl::_prepareCommand(int streamId, enum plPriv::RemoteCommandType ct, int payloadSize)
{
    bsVec<u8>* txBuffer = _clientCnx->getTxBuffer(streamId);
    if(txBuffer) {
        int commandSize = 2+payloadSize;
        txBuffer->resize(8/*header*/+commandSize);
        (*txBuffer)[0] = 'P';
        (*txBuffer)[1] = 'L';
        (*txBuffer)[2] = ((int)plPriv::PL_DATA_TYPE_CONTROL>>8)&0xFF;
        (*txBuffer)[3] = ((int)plPriv::PL_DATA_TYPE_CONTROL>>0)&0xFF;
        (*txBuffer)[4] = (commandSize>>24)&0xFF;
        (*txBuffer)[5] = (commandSize>>16)&0xFF;
        (*txBuffer)[6] = (commandSize>> 8)&0xFF;
        (*txBuffer)[7] = (commandSize>> 0)&0xFF;
        (*txBuffer)[8] = (u8)(((int)ct)>>8);
        (*txBuffer)[9] = (u8)(((int)ct)&0xFF);
    }
    return txBuffer;
}


bool
cmLiveControl::remoteSetMaxLatencyMs(int streamId, int latencyMs)
{
    bsVec<u8>* txBuffer = _prepareCommand(streamId, plPriv::PL_CMD_SET_MAX_LATENCY, 2);
    if(!txBuffer) return false;
    // Fill payload and send
    (*txBuffer)[10] = (latencyMs>>8)&0xFF;
    (*txBuffer)[11] = (latencyMs>>0)&0xFF;
    _clientCnx->sendTxBuffer(streamId);
    return true;
}


bool
cmLiveControl::remoteSetFreezeMode(int streamId, bool state)
{
    // Get the buffer
    bsVec<u8>* txBuffer = _prepareCommand(streamId, plPriv::PL_CMD_SET_FREEZE_MODE, 1);
    if(!txBuffer) return false;
    // Fill payload and send
    (*txBuffer)[10] = state? 1:0;
    _clientCnx->sendTxBuffer(streamId);
    return true;
}


bool
cmLiveControl::remoteStepContinue(int streamId, u64 bitmap)
{
    // Get the buffer
    bsVec<u8>* txBuffer = _prepareCommand(streamId, plPriv::PL_CMD_STEP_CONTINUE, 8);
    if(!txBuffer) return false;
    // Fill payload and send
    (*txBuffer)[10] = (bitmap>>56)&0xFF;
    (*txBuffer)[11] = (bitmap>>48)&0xFF;
    (*txBuffer)[12] = (bitmap>>40)&0xFF;
    (*txBuffer)[13] = (bitmap>>32)&0xFF;
    (*txBuffer)[14] = (bitmap>>24)&0xFF;
    (*txBuffer)[15] = (bitmap>>16)&0xFF;
    (*txBuffer)[16] = (bitmap>> 8)&0xFF;
    (*txBuffer)[17] = (bitmap>> 0)&0xFF;
    _clientCnx->sendTxBuffer(streamId);
    return true;
}


bool
cmLiveControl::remoteKillProgram(int streamId)
{
    // Get the buffer
    bsVec<u8>* txBuffer = _prepareCommand(streamId, plPriv::PL_CMD_KILL_PROGRAM, 0);
    if(!txBuffer) return false;
    // Send
    _clientCnx->sendTxBuffer(streamId);
    return true;
}


bool
cmLiveControl::remoteCli(int streamId, const bsVec<bsString>& commands)
{
    // Compute the payload length
    int payloadLength = 2;
    for(const bsString& c : commands) payloadLength += (int)strlen(c.toChar())+1;

    // Get the buffer
    bsVec<u8>* txBuffer = _prepareCommand(streamId, plPriv::PL_CMD_CALL_CLI, payloadLength);
    if(!txBuffer) return false;

    // Fill payload
    (*txBuffer)[10] = (commands.size()>>8)&0xFF;
    (*txBuffer)[11] = (commands.size()>>0)&0xFF;
    int offset = 12;
    for(const bsString& c : commands) {
        int sLen = (int)strlen(c.toChar())+1; // +1 = zero termination
        memcpy(&((*txBuffer)[offset]), c.toChar(), sLen);
        offset += sLen;
    }
    plAssert(offset==10+payloadLength);

    // Send
    _clientCnx->sendTxBuffer(streamId);
    return true;
}
