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


// This files implements the core of the scripting module, based on the server/common library
//  which handles the event recording.

// Internal
#define PL_IMPLEMENTATION 1
#include "bsOs.h"
#include "bsHashMap.h"
#include "cmCnx.h"
#include "cmLiveControl.h"
#include "cmRecording.h"
#include "cmCompress.h"
#include "pyMainItf.h"

#include "pyInterface.h"


// Some constants
constexpr int RECORD_CACHE_MB = 30;


// ==============================================================================================
// Main library handler
// ==============================================================================================

pyMainItf::pyMainItf(int rxPort, pyiNotifications ntf)
{
    // Initialize the compression
    cmInitChunkCompress();

    // Internals
    _ntf       = new pyiNotifications(ntf);
    _recording = new cmRecording(this, "records", true); // Note: this storage path will not be used
    _clientCnx = new cmCnx(this, rxPort);
    _live      = new cmLiveControl(this, _clientCnx);

    // Some memory reservation
    _specs.reserve(64);
    _elemSpecContexts.reserve(1024);
    _batchedEvents.reserve(16384);
}


pyMainItf::~pyMainItf(void)
{
    delete _live;
    delete _recording;
    delete _clientCnx;
    delete _ntf;
    cmUninitChunkCompress();
}


// =====================================
// Log service
// =====================================

void
pyMainItf::log(cmLogKind kind, const bsString& msg)
{
    _ntf->notifyLog((int)kind, msg.toChar());
}


void
pyMainItf::log(cmLogKind kind, const char* format, ...)
{
    // Format the string
    char tmpStr[256];
    va_list args;
    va_start(args, format);
    vsnprintf(tmpStr, sizeof(tmpStr), format, args);
    va_end(args);
    // Store
    _ntf->notifyLog((int)kind, tmpStr);
}


// =====================================
// Interaction with the client reception
// =====================================

// From cmCnx thread
void
pyMainItf::notifyNewRemoteBuffer(bsVec<u8>& buffer)
{
    _live->storeNewRemoteBuffer(buffer);
}


// From cmCnx thread via live control
void
pyMainItf::notifyNewFrozenThreadState(u64 frozenThreadBitmap)
{
    notifyScript(); // Update the script side first, to ensure all is synchronised with freeze points
    _ntf->notifyNewFrozenThreadState(frozenThreadBitmap);
}


// From cmCnx thread via live control
void
pyMainItf::notifyCommandAnswer(plPriv::plRemoteStatus status, const bsString& answer)
{
    _ntf->notifyCommandAnswer(status, answer.toChar());
}


// From cmCnx thread via live control
void
pyMainItf::notifyNewCli(u32 nameIdx, int paramSpecIdx, int descriptionIdx)
{
    _batchedClis.push_back({_recording->getString(nameIdx).toChar(), _recording->getString(paramSpecIdx).toChar(),
            _recording->getString(descriptionIdx).toChar()});
}


bool
pyMainItf::notifyRecordStarted(const bsString& appName, const bsString& buildName, int protocol, s64 timeTickOrigin, double tickToNs,
                               bool areStringsExternal, bool isStringHashShort, bool isControlEnabled, bool isDateShort)
{
    bsString errorMsg;
    _recording->beginRecord(appName, buildName, protocol, timeTickOrigin, tickToNs,
                            areStringsExternal, isDateShort, RECORD_CACHE_MB, errorMsg, false);
    if(!errorMsg.empty()) {
        notifyErrorForDisplay(ERROR_GENERIC, errorMsg);
        return false;
    }

    // Compute the spec hashes, now that we know the string hash size
    _isStringHashShort  = isStringHashShort;
    _areStringsExternal = areStringsExternal;

    _ntf->notifyRecordStarted(appName.toChar(), buildName.toChar(), areStringsExternal, isStringHashShort, isControlEnabled);

    std::lock_guard<std::mutex> lk(_mx);
    // Clean the event specifications, so they can manage the new record
    for(Spec& f : _specs) {
        f.threadHash = 0;
        for(SpecElemToken& set : f.parentPath) set.hash = 0;
        for(SpecElem& se : f.elems) {
            for(SpecElemToken& set : se.tokens) set.hash = 0;
            se.resolution = NO_ELEMENTS_SEEN;
        }
        f.parentElemIdx = -1;
        f.isOpenParent = false;
        f.events.clear();
        computeSpecHashes(f);
    }
    _elemSpecContexts.clear();
    _lastDateNs = 0;
    plAssert(_batchedEvents.empty());
    _isRecordOnGoing = true;
    return true;
}


bool
pyMainItf::notifyNewEvents(plPriv::EventExt* events, int eventQty)
{
    return _recording->storeNewEvents(events, eventQty);
}


void
pyMainItf::notifyNewString(const bsString& newString, u64 hash)
{
    // Record the new string
    const bsString& storedString = _recording->storeNewString(newString, hash);

    // Batch it for external notification
    _batchedStrings.push_back({ hash, storedString.toChar() });
}


void
pyMainItf::notifyNewCollectionTick(void)
{
    _didCollectionTickOccurred = true;
    notifyScript();
}


void
pyMainItf::notifyNewThread(int threadId, u64 nameHash)
{
    // Batch it for external notification
    _batchedThreads.push_back({ nameHash, threadId });
}


pyMainItf::ResolutionState
pyMainItf::matchPath(int& startElemIdx, const bsVec<SpecElemToken>& tokens)
{
    bool isSuperWildcard = false;
    u64  elemNameHash;
    int  elemPrevElemIdx, elemThreadId;
    ResolutionState matchState = NO_MATCHING_NAME;

    // Iterate on the specification tokens from the bottom, as the element is a leaf
    for(int i=tokens.size()-1; i>=0; --i) {
        const SpecElemToken& ft = tokens[i];

        // Token types:  0: elem hash   1: "*" skip 1 level   2: "**" multi-level wildcard   3: "." exactly the parent
        // Token type 3 is ignored here, it is checked after the match.
        if     (ft.tokenType==2) isSuperWildcard = true;
        else if(ft.tokenType<=1) { // Element hash case or one level skip
            if(startElemIdx<0) return matchState;
            bool workOnce = true;
            while(startElemIdx>=0 && (isSuperWildcard || workOnce)) {
                workOnce = false;
                _recording->getElemInfos(startElemIdx, &elemNameHash, &elemPrevElemIdx, &elemThreadId);
                if(ft.tokenType==0 && ft.hash!=elemNameHash) {
                    if(!isSuperWildcard) {
                        return matchState;
                    }
                }
                else isSuperWildcard = false; // A match (or joker '*') cancels this state
                startElemIdx = elemPrevElemIdx;
                if(matchState<NO_MATCHING_PATH) matchState = NO_MATCHING_PATH;
            }
        }
    }

    return RESOLVED;
}


void
pyMainItf::notifyNewElem(u64 nameHash, int elemIdx, int prevElemIdx, int threadId, int flags)
{
    // Batch it for external notification
    _batchedElems.push_back({ nameHash, elemIdx, prevElemIdx, threadId, flags });

    // Resize the lookup
    std::lock_guard<std::mutex> lk(_mx);
    while(_elemSpecContexts.size()<=elemIdx) {
        _elemSpecContexts.push_back({});
    }
    _elemSpecContexts[elemIdx].isDeclared = true;
    _elemSpecContexts[elemIdx].nameHash = nameHash;
    resolveSpecs(elemIdx, threadId);
}


bool
pyMainItf::createDeltaRecord(void)
{
    notifyScript();
    return true;
}


void
pyMainItf::notifyRecordEnded(bool isRecordOk)
{
    (void) isRecordOk;

    // Stop the recording
    _recording->endRecord();

    // Flush the unfinished collected events (parent closing)
    for(Spec& f : _specs) {
        if(!f.isOpenParent || f.events.empty()) continue;
        f.events[0].childrenQty = f.events.size()-1;
        f.events[0].value       = _lastDateNs-f.events[0].dateNs;
        // Move to main buffer
        _batchedEvents.resize(_batchedEvents.size()+f.events.size());
        memcpy(&_batchedEvents[_batchedEvents.size()-f.events.size()], &f.events[0], f.events.size()*sizeof(pyiEvent));
        f.events.clear();
        f.isOpenParent = false;
    }

    // Notify for the last time
    notifyScript();

    // Reset the connection state
    std::lock_guard<std::mutex> lk(_mx);
    _isRecordOnGoing = false;
    _ntf->notifyRecordEnded();
}


void
pyMainItf::notifyScript(void)
{
    // Batch notify the new items (no need for lock, as all happens in the same thread)

    // Strings
    if(!_batchedStrings.empty()) {
        _ntf->notifyNewStrings(&_batchedStrings[0], _batchedStrings.size());
        _batchedStrings.clear();
    }

    // Threads
    if(!_batchedThreads.empty()) {
        _ntf->notifyNewThreads(&_batchedThreads[0], _batchedThreads.size());
        _batchedThreads.clear();
    }

    // Elements
    if(!_batchedElems.empty()) {
        _ntf->notifyNewElems(&_batchedElems[0], _batchedElems.size());
        _batchedElems.clear();
    }

    // Clis
    if(!_batchedClis.empty()) {
        _ntf->notifyNewClis(&_batchedClis[0], _batchedClis.size());
        _batchedClis.clear();
    }

    // Events
    // Need a mutex due to addSpec/clearAllSpecs/clearBufferedEvents calls from script side (commands, not notifications)
    std::lock_guard<std::mutex> lk(_mx);
    if(!_batchedEvents.empty()) {
        _ntf->notifyNewEvents(&_batchedEvents[0], _batchedEvents.size());
        _batchedEvents.clear();
    }

    // Collection tick
    if(_didCollectionTickOccurred) {
        _ntf->notifyNewCollectionTick();
        _didCollectionTickOccurred = false;
    }
}


void
pyMainItf::notifyInstrumentationError(cmRecord::RecErrorType type, int threadId, u32 filenameIdx, int lineNbr, u32 nameIdx)
{
    constexpr const char* errorMessagesPerType[cmRecord::ERROR_REC_TYPE_QTY] = {
        "Maximum thread quantity reached", "Unbalanced begin/end blocks",
        "Maximum nesting level quantity reached", "Dropped data events because outside a scope", "End scope name does not match the begin scope"
    };
    plAssert(type<cmRecord::ERROR_REC_TYPE_QTY);
    plAssert(filenameIdx>=0);
    char errorMsg[256];
    snprintf(errorMsg, sizeof(errorMsg), "%s:%d - %s:%s - %s",
             (filenameIdx==0xFFFFFFFF)? "N/A (marker)" : _recording->getString(filenameIdx).toChar(), lineNbr,
             (type==cmRecord::ERROR_MAX_THREAD_QTY_REACHED || _recording->getThreadNameIdx(threadId)<0)? "(no thread)" : _recording->getString(_recording->getThreadNameIdx(threadId)).toChar(),
             _recording->getString(nameIdx).toChar(), errorMessagesPerType[type]);

    // Log the instrumentation error message
    log(LOG_ERROR, "%s", errorMsg);
}


// Called by any thread
void
pyMainItf::notifyErrorForDisplay(cmErrorKind kind, const bsString& errorMsg)
{
    log(LOG_ERROR, errorMsg);
}


void
pyMainItf::clearAllSpecs(void)
{
    std::lock_guard<std::mutex> lk(_mx);
    _specs.clear();
    _batchedEvents.clear();
    // Clears only the specs, not the elements
    for(int i=0; i<_elemSpecContexts.size(); ++i) _elemSpecContexts[i].reset();
}


void
pyMainItf::clearBufferedEvents(void)
{
    std::lock_guard<std::mutex> lk(_mx);
    for(auto& f : _specs) {
        f.events.clear();
        f.isOpenParent = false;
    }
    _batchedEvents.clear();
}


void
pyMainItf::addSpec(const char* threadName, pyiSpec* parentPath, pyiSpec* elemArray, int elemQty)
{
    std::lock_guard<std::mutex> lk(_mx);

    // Get the new empty spec
    _specs.push_back({});
    Spec& f = _specs.back();
    f.events.reserve(128);

    // Copy the fields
    f.threadName = threadName;
    f.parentPath.resize(parentPath->pathQty);
    for(int i=0; i<parentPath->pathQty; ++i) {
        const char* s = parentPath->path[i];
        if     (!strcmp(s, "."))  f.parentPath[i] = { "", 3 };
        else if(!strcmp(s, "**")) f.parentPath[i] = { "", 2 };
        else if(!strcmp(s, "*"))  f.parentPath[i] = { "", 1 };
        else                      f.parentPath[i] = {  s, 0 };
    }
    f.elems.resize(elemQty);
    for(int j=0; j<elemQty; ++j) {
        f.elems[j].resolution = NO_ELEMENTS_SEEN;
        f.elems[j].tokens.resize(elemArray[j].pathQty);
        for(int i=0; i<elemArray[j].pathQty; ++i) {
            const char* s = elemArray[j].path[i];
            if     (!strcmp(s, "."))  f.elems[j].tokens[i] = { "", 3 };
            else if(!strcmp(s, "**")) f.elems[j].tokens[i] = { "", 2 };
            else if(!strcmp(s, "*"))  f.elems[j].tokens[i] = { "", 1 };
            else                      f.elems[j].tokens[i] = {  s, 0 };
       }
    }

    if(_isRecordOnGoing) {
        // Compute the string hashes (as a record is running, we know if it is a 32 or 64 bits hash)
        computeSpecHashes(f);

        // Check resolution on the already present elements (later resolutions will be performed only with newly added elements)
        u64 elemNameHash;
        int elemPrevElemIdx, elemThreadId;
        for(int elemIdx=0; elemIdx<_elemSpecContexts.size(); ++elemIdx) {
            if(!_elemSpecContexts[elemIdx].isDeclared) continue;
            _recording->getElemInfos(elemIdx, &elemNameHash, &elemPrevElemIdx, &elemThreadId);
            resolveSpecs(elemIdx, elemThreadId);
        }
    }
}


void
pyMainItf::computeSpecHashes(Spec& f)
{
    // Thread name
    if(f.threadName.empty()) f.threadHash = 0;
    else f.threadHash = _isStringHashShort? bsHash32String(f.threadName.toChar()) : bsHashString(f.threadName.toChar());
    // Parent path
    for(SpecElemToken& set : f.parentPath) {
        set.hash = _isStringHashShort? bsHash32String(set.name.toChar()) : bsHashString(set.name.toChar());
    }
    // Events path
    for(SpecElem& se : f.elems) {
        for(SpecElemToken& set : se.tokens) {
            set.hash = _isStringHashShort? bsHash32String(set.name.toChar()) : bsHashString(set.name.toChar());
        }
    }
}


void
pyMainItf::resolveSpecs(int elemIdx, int threadId)
{
    // Try to resolve specs
    u64 elemNameHash;
    int elemPrevElemIdx, elemThreadId;
    for(int fId=0; fId<_specs.size(); ++fId) {
        Spec& f = _specs[fId];
        // Loop on elements specs from this spec
        for(SpecElem& specElem : f.elems) {
            const bsVec<SpecElemToken>& elemPath = specElem.tokens;

            if(specElem.resolution<NO_MATCHING_THREAD) specElem.resolution = NO_MATCHING_THREAD;
            if(f.threadHash!=0 && _recording->getThreadNameHash(threadId)!=f.threadHash) continue;  // A null hash for thread is a wildcard
            if(specElem.resolution<NO_MATCHING_NAME) specElem.resolution = NO_MATCHING_NAME;

            // Match the element spec from bottom
            int eIdx = elemIdx;
            ResolutionState matchState = matchPath(eIdx, elemPath);
            if(matchState!=RESOLVED) {
                if(specElem.resolution<matchState) specElem.resolution = matchState;
                continue;
            }

            if(!f.parentPath.empty()) {
                if(specElem.resolution<NO_MATCHING_PARENT_NAME) specElem.resolution = NO_MATCHING_PARENT_NAME;

                // List the potential parents
                int potentialParentIdx[32]; // Maximum path depth is 32 by design
                int potentialParentQty = 0;
                int firstElemIdx       = eIdx;
                while(eIdx>=0) {
                    _recording->getElemInfos(eIdx, &elemNameHash, &elemPrevElemIdx, &elemThreadId);
                    if(elemNameHash==f.parentPath.back().hash) potentialParentIdx[potentialParentQty++] = eIdx;
                    eIdx = elemPrevElemIdx;
                }
                if(potentialParentQty>0 && specElem.resolution<NO_MATCHING_PARENT_PATH) {
                    specElem.resolution = NO_MATCHING_PARENT_PATH;
                }

                // Match the parent spec starting with root-closest elements of the same hash
                int matchingParentIdx = -1;
                for(int i=potentialParentQty-1; i>=0; --i) {
                    eIdx = potentialParentIdx[i];
                    if(matchPath(eIdx, f.parentPath)!=RESOLVED) continue;
                    if(elemPath[0].tokenType==3 && potentialParentIdx[i]!=firstElemIdx) { // Element's "." constraint
                        if(specElem.resolution<NO_MATCHING_ELEM_ROOT) specElem.resolution = NO_MATCHING_ELEM_ROOT;
                        continue;
                    }
                    if(f.parentPath[0].tokenType==3 && eIdx>=0) { // Parent's  "." constraint
                        if(specElem.resolution<NO_MATCHING_PARENT_ROOT) specElem.resolution = NO_MATCHING_PARENT_ROOT;
                        continue;
                    }
                    matchingParentIdx = potentialParentIdx[i];
                    break;
                }

                // Store the parent or check its validity and unicity
                if(matchingParentIdx<0) continue; // Parent not found
                if(f.parentElemIdx<0) { // First matching parent: it is our reference now
                    f.parentElemIdx = matchingParentIdx;
                    // Activate the spec on it
                    _elemSpecContexts[matchingParentIdx].isActive = true;
                    _elemSpecContexts[matchingParentIdx].specs.push_back({fId});
                    _elemSpecContexts[matchingParentIdx].specs.back().beginEvent.specId = -1;
                    //printf(" RESOLVED PARENT %d %lx\n", matchingParentIdx, f.parentPath.back().hash);
                }
                if(matchingParentIdx!=f.parentElemIdx) {
                    if(specElem.resolution<INCONSISTENT_PARENT) specElem.resolution = INCONSISTENT_PARENT;
                    continue; // The parent is not consistent with our reference, so we skip it
                }
            }
            else if(elemPath[0].tokenType==3 && eIdx>=0) { // Element's "." constraint (without parent, "." means "root")
                if(specElem.resolution<NO_MATCHING_ELEM_ROOT) specElem.resolution = NO_MATCHING_ELEM_ROOT;
                continue;
            }
            // Activate the spec on this element
            _elemSpecContexts[elemIdx].isActive = true;
            _elemSpecContexts[elemIdx].specs.push_back({fId});
            _elemSpecContexts[elemIdx].specs.back().beginEvent.specId = -1;
            specElem.resolution = RESOLVED;
            //printf(" RESOLVED ELEM %d %lx\n", elemIdx, elemPath.back().hash);
        }
    }
}


void
pyMainItf::notifyFilteredEvent(int elemIdx, int flags, u64 nameHash, s64 dateNs, u64 value)
{
    if(dateNs>_lastDateNs) _lastDateNs = dateNs;
    if(flags&PL_FLAG_SCOPE_BEGIN) _elemSpecContexts[elemIdx].lastBeginDateNs = dateNs;
    if(!_elemSpecContexts[elemIdx].isActive) return;

    std::lock_guard<std::mutex> lk(_mx); // Not great because for each exported event, but no real choice

    // Loop on associated specs (an element can be part of several specs)
    for(ElemSpecCtx& specCtx : _elemSpecContexts[elemIdx].specs) {
        Spec& f = _specs[specCtx.specId];

        // Fix the lost initial parent "begin" either due to:
        //  1) activation of the parent only when one child is activated, so too late for the parent "begin"
        //  2) specification added in the middle of a run, potentially making a "begin" missing
        if(elemIdx!=f.parentElemIdx) {
            if(!f.parentPath.empty() && !f.isOpenParent) {
                f.isOpenParent = true;
                plAssert(f.events.empty());
                f.events.push_back({specCtx.specId, f.parentElemIdx, 0, _elemSpecContexts[f.parentElemIdx].nameHash,
                        _elemSpecContexts[f.parentElemIdx].lastBeginDateNs, 0});
            }
            if((flags&PL_FLAG_SCOPE_END) && specCtx.beginEvent.specId<0) {
                specCtx.beginEvent = {specCtx.specId, elemIdx, 0, nameHash, _elemSpecContexts[elemIdx].lastBeginDateNs, 0};
            }
        }

        // Case parent
        if(elemIdx==f.parentElemIdx) {
            if(!f.isOpenParent && (flags&PL_FLAG_SCOPE_BEGIN)) {
                f.isOpenParent = true;
                plAssert(f.events.empty());
                f.events.push_back({specCtx.specId, elemIdx, 0, nameHash, dateNs, 0});
            }
            else if (f.isOpenParent && (flags&PL_FLAG_SCOPE_END)) {
                f.isOpenParent = false;
                plAssert(!f.events.empty());
                f.events[0].childrenQty = f.events.size()-1;
                f.events[0].value       = dateNs-f.events[0].dateNs;
                // Move to main buffer
                _batchedEvents.resize(_batchedEvents.size()+f.events.size());
                memcpy(&_batchedEvents[_batchedEvents.size()-f.events.size()], &f.events[0], f.events.size()*sizeof(pyiEvent));
                f.events.clear();
            }
        }

        // Case children of a parent or standalone
        else {
            if(flags&PL_FLAG_SCOPE_BEGIN) {
                specCtx.beginEvent = {specCtx.specId, elemIdx, 0, nameHash, dateNs, value};
            }
            else if(flags&PL_FLAG_SCOPE_END) {
                plAssert(specCtx.beginEvent.specId>=0);
                specCtx.beginEvent.value = dateNs-specCtx.beginEvent.dateNs;
                if(f.isOpenParent)            f.events.push_back      (specCtx.beginEvent);
                else if(f.parentPath.empty()) _batchedEvents.push_back(specCtx.beginEvent);
                specCtx.beginEvent.specId = -1;
            }
            else {
                if(f.isOpenParent)            f.events.push_back      ({specCtx.specId, elemIdx, 0, nameHash, dateNs, value});
                else if(f.parentPath.empty()) _batchedEvents.push_back({specCtx.specId, elemIdx, 0, nameHash, dateNs, value});
            }
        }
    } // End of loop on specs for this element
}


void
pyMainItf::setRecordingConfig(bool isEnabled, const char* forcedFilename)
{
    _recording->setRecordingConfig(isEnabled, forcedFilename);
}


void
pyMainItf::getUnresolvedElemInfos(pyiDebugSpecInfo** infoArray, int* infoQty)
{
    static const char* stringifiedState[RESOLUTION_STATE_QTY] = \
        {
         "No events in record to match with", "No matching thread", "No matching event name", "No matching event path",
         "No matching parent event name", "No matching parent event path", "'.' is not matching the event's root", "'.' is not matching the parent event's root",
         "Inconsistent parent events, it shall be the same for all events", "Resolved"
        };

    std::lock_guard<std::mutex> lk(_mx);

    // Collect the data
    _debugSpecInfos.clear(); // Class member so that it is persistent inside the script library
    for(int fId=0; fId<_specs.size(); ++fId) {
        const Spec& f = _specs[fId];
        for(int elemId=0; elemId<f.elems.size(); ++elemId) {
            const SpecElem& specElem = f.elems[elemId];
            if(specElem.resolution==RESOLVED) continue;
            _debugSpecInfos.push_back({ fId, elemId });
            snprintf((char*)&_debugSpecInfos.back().errorMsg, sizeof(pyiDebugSpecInfo::errorMsg), "%s", stringifiedState[(int)specElem.resolution]);
        }
    }

    // Return the infos by filling the input pointers
    *infoArray = _debugSpecInfos.empty()? 0 : &_debugSpecInfos[0];
    *infoQty   = _debugSpecInfos.size();
}
