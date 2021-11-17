// The MIT License (MIT)
//
// Copyright(c) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


// This file is the implementation of the instrumentation library for Python language,
//  as a C-extension. It mainly wraps the C++ instrumentation library.


// System includes
#include <mutex>
#include <vector>

// CPython includes
#include "Python.h"
#include "frameobject.h"

// Configure and implement the C++ Palanteer instrumentation
#define USE_PL 1
#define PL_VIRTUAL_THREADS 1
#define PL_IMPLEMENTATION 1
#define PL_IMPL_OVERLOAD_NEW_DELETE 0    // Overload from a dynamic library does not work
#define PL_IMPL_MAX_CLI_QTY              1024
#define PL_IMPL_DYN_STRING_QTY           4096
#define PL_IMPL_MAX_EXPECTED_STRING_QTY 16384
#define PL_PRIV_IMPL_LANGUAGE "Python"
#define PL_GROUP_PL_VERBOSE 0            // Do not profile the Palanteer threads
#include "palanteer.h"


// Module definitions
struct ThreadStackElem_t {
    plPriv::hashStr_t filenameHash;
    plPriv::hashStr_t nameHash;
    int               lineNbr;
};
constexpr int STACK_MAX_DEPTH = 256;
struct pyCommonThreadCtx_t {
    // Thread common fields
    PyFrameObject*    nextExceptionFrame = 0;                // To manage unwinding
    int               filterOutDepth     = STACK_MAX_DEPTH;  // Filtering out from this level and below
    int               stackDepth         = 0;
    ThreadStackElem_t stack[STACK_MAX_DEPTH];   // 5 KB per thread
};
struct pyOsThreadCtx_t {
    int            isBootstrap = 1;  // First events ending a scope shall be skipped, until another kind of event is received
    PyFrameObject* currentCoroutineFrame = 0;
    int            isWorkerNameDeclared = 0;
};
struct CoroutineNaming {
    int  namingCount = 0;
    char name[PL_DYN_STRING_MAX_SIZE];
};

// Module state
thread_local static pyOsThreadCtx_t gOsThread;  // State for OS threads
static std::mutex          gGlobMutex; // Protects all containers below
static pyCommonThreadCtx_t gThreads[PL_MAX_THREAD_QTY];  // State for all threads (OS and coroutine)
static plPriv::FlatHashTable<plPriv::hashStr_t> gHashStrLookup;    // Hashed object   -> Palanteer string hash
static plPriv::FlatHashTable<PyObject*>         gCliHandlerLookup; // Hashed CLI name -> Python callable object
static std::vector<CoroutineNaming>             gCoroutineNames;     // Array of (name, name usage count)
static plPriv::FlatHashTable<int>               gCoroutineNameToIdx; // Hashed name  -> coroutine name index in array above
static plPriv::FlatHashTable<int>               gSuspendedFrames;
static int                                      gAsyncWorkerCount = 0;
static PyMemAllocatorEx gOldAllocatorRaw;
static PyMemAllocatorEx gNewAllocatorRaw;
static bool gIsEnabled      = false;
static bool gWithFunctions  = false;
static bool gWithExceptions = false;
static bool gWithMemory     = false;
static bool gWithCCalls     = false;

// Filtering out some automatic instrumentation
static plPriv::FlatHashTable<int> gFilterOutClassName;    // Used as hashset
static plPriv::FlatHashTable<int> gFilterOutFunctionName; // Used as hashset
static plPriv::FlatHashTable<int> gFilterOutObject; // Used as hashset

plPriv::hashStr_t gFilterOutClassDb[] = {
    PL_STRINGHASH("palanteer._cextension"),
    PL_STRINGHASH("_UnixSelectorEventLoop"),  // Coroutine mechanism on Linux
    PL_STRINGHASH("ProactorEventLoop"),       // Coroutine mechanism on Windows
    0 };
// Mask function and all sub calls
plPriv::hashStr_t gFilterOutFunctionAndBelowDb[] = {
    PL_STRINGHASH("Thread._bootstrap"), PL_STRINGHASH("Thread._bootstrap_inner"), // Mask thread creation "leave" events, as "enter" is not seen (cf bootstrap mechanism)
    PL_STRINGHASH("_find_and_load"),      // Python bootstrap
    PL_STRINGHASH("TimerHandle.cancel"),  // Coroutine mechanism
    PL_STRINGHASH("_cancel_all_tasks"),   // Coroutine mechanism
    0
};
// Mask only these function level, not below
plPriv::hashStr_t gFilterOutFunctionDb[] = {
    PL_STRINGHASH("_plFunctionInner"),             // Mask plFunction decorator inner function
    PL_STRINGHASH("_pl_garbage_collector_notif"),  // Mask the GC tracking glue
    PL_STRINGHASH("Thread.run"),  // Mask the Thread glue
    0
};


// Python compatibility
#if PY_VERSION_HEX<0x030a00b1
  #define USE_TRACING_ACCESS use_tracing
#else
  #define USE_TRACING_ACCESS cframe->use_tracing
#endif


// Debug macro
// ===========

#define CheckForPyError()                                               \
    if (PyErr_Occurred()) {                                             \
        PyObject *ptype, *pvalue, *ptraceback;                          \
        PyErr_Fetch(&ptype, &pvalue, &ptraceback);                      \
        printf("PYTHON ERROR DETECTED: %s\n", PyUnicode_AsUTF8(pvalue)); \
    }

#define PrintPyObject(obj)                                              \
    {                                                                   \
        PyObject* repr = PyObject_Repr(obj);                            \
        printf("REPR: %s\n", PyUnicode_AsUTF8(repr));                   \
        Py_XDECREF(repr);                                               \
    }


// Helpers
// =======

inline
plPriv::hashStr_t
pyHashPointer(const void* p) {
    plPriv::hashStr_t strHash = PL_FNV_HASH_OFFSET_;
    plPriv::hashStr_t s = (uintptr_t)p;
    strHash = (strHash^((s>> 0)&0xFFLL))*PL_FNV_HASH_PRIME_;
    strHash = (strHash^((s>> 8)&0xFFLL))*PL_FNV_HASH_PRIME_;
    strHash = (strHash^((s>>16)&0xFFLL))*PL_FNV_HASH_PRIME_;
    strHash = (strHash^((s>>24)&0xFFLL))*PL_FNV_HASH_PRIME_;
    strHash = (strHash^((s>>32)&0xFFLL))*PL_FNV_HASH_PRIME_;
    strHash = (strHash^((s>>40)&0xFFLL))*PL_FNV_HASH_PRIME_;
    strHash = (strHash^((s>>48)&0xFFLL))*PL_FNV_HASH_PRIME_;
    strHash = (strHash^((s>>56)&0xFFLL))*PL_FNV_HASH_PRIME_;
    return (strHash==0)? 1 : strHash; // Zero is a reserved value
}

#define CACHE_STRING(s, outputHash)                                     \
    strHash    = pyHashPointer(s);                                      \
    outputHash = 0;                                                     \
    if(!gHashStrLookup.find(strHash, outputHash)) {                     \
        gHashStrLookup.insert(strHash, plPriv::hashString(s));          \
    }


inline
void
pyGetNameFilenameLineNbr(const char* name,
                         const char*& filename, int& lineNbr, plPriv::hashStr_t& objectFilenameHash,
                         plPriv::hashStr_t& palanteerFilenameStrHash, plPriv::hashStr_t& palanteerNameStrHash)
{
    // Module/filename and line number
    PyThreadState* threadState = PyThreadState_GET();
    plAssert(threadState && threadState->frame);

    // Get the line number
    lineNbr = PyFrame_GetLineNumber(threadState->frame);

    // Get the filename
    filename = 0; palanteerFilenameStrHash = 0;
    PyCodeObject* objectCode = threadState->frame->f_code;
    objectFilenameHash       = pyHashPointer(objectCode->co_filename);

    // Note: We must not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
    gGlobMutex.lock();
    // Get the name hash (or null if it is a new string)
    plPriv::hashStr_t strHash;
    CACHE_STRING(name, palanteerNameStrHash);
    // Get the filename hash
    bool isObjectFoundInCache = gHashStrLookup.find(objectFilenameHash, palanteerFilenameStrHash);
    gGlobMutex.unlock();

    if(!isObjectFoundInCache) {
        filename = PyUnicode_AsUTF8(objectCode->co_filename);
        gGlobMutex.lock();
        gHashStrLookup.insert(objectFilenameHash, plPriv::hashString(filename)); // We let the palanteerFilenameStrHash set to zero because it is a dynamic string
        gGlobMutex.unlock();
        plAssert(filename);
    }
    else plAssert(palanteerFilenameStrHash);
    // Else filename is still zero because already known by Palanteer, so similar to a static string
}


inline
void
pyEventLogRaw(plPriv::hashStr_t filenameHash, plPriv::hashStr_t nameHash, const char* filename, const char* name,
              int lineNbr, int flags, uint64_t v) {
    plAssert(filenameHash || filename);
    plAssert(nameHash     || name);
    const char* allocFileStr = filenameHash? 0 : plPriv::getDynString(filename);
    const char* allocNameStr = nameHash?     0 : plPriv::getDynString(name);
    uint32_t bi = plPriv::globalCtx.bankAndIndex.fetch_add(1);
    plPriv::EventInt& e = plPriv::eventLogBase(bi, filenameHash, nameHash, filenameHash? 0 : allocFileStr, nameHash? 0 : allocNameStr, lineNbr, flags);
    e.vU64  = v;
    e.magic = bi;
    plPriv::eventCheckOverflow(bi);
}


inline
void
pyEventLogRawString(plPriv::hashStr_t filenameHash, plPriv::hashStr_t nameHash, const char* filename, const char* name,
                    int lineNbr, plPriv::hashStr_t valueStrHash, const char* valueStr) {
    plAssert(filenameHash || filename);
    plAssert(nameHash     || name);
    plAssert(valueStrHash || valueStr);
    const char* allocFileStr  = filenameHash? 0 : plPriv::getDynString(filename);
    const char* allocNameStr  = nameHash?     0 : plPriv::getDynString(name);
    const char* allocValueStr = valueStrHash? 0 : plPriv::getDynString(valueStr);
    uint32_t bi = plPriv::globalCtx.bankAndIndex.fetch_add(1);
    plPriv::EventInt& e = plPriv::eventLogBase(bi, filenameHash, nameHash, filenameHash? 0 : allocFileStr, nameHash? 0 : allocNameStr, lineNbr, PL_FLAG_TYPE_DATA_STRING);
    e.vString.hash  = valueStrHash;
    e.vString.value = allocValueStr;
    e.magic = bi;
    plPriv::eventCheckOverflow(bi);
}


static void
logFunctionEvent(PyObject* Py_UNUSED(self), PyFrameObject* frame, PyObject* arg, bool isEnter, bool calledFromC)
{
    char tmpStr[PL_DYN_STRING_MAX_SIZE+32];  // 32 is margin to avoid truncation in some cases (coroutine name prefix)

    if(gOsThread.isBootstrap) {
        if(!isEnter) return;        // First "leaving" events are dropped until an "enter" is found
        gOsThread.isBootstrap = 0;  // End of bootstrap phase
    }

    // Co-routine management
    // =====================

#define HASH_FRAME(frame) (uint32_t)((uintptr_t)frame)
    bool isCoroutineNew = false;
    bool isCoroutine = (frame->f_code->co_flags & (CO_COROUTINE|CO_ITERABLE_COROUTINE|CO_ASYNC_GENERATOR)) && !calledFromC;
#if PY_MINOR_VERSION >= 10
    bool isCoroutineSuspended = isCoroutine && !isEnter && (frame->f_state==FRAME_SUSPENDED);
#else
    bool isCoroutineSuspended = isCoroutine && !isEnter && (frame->f_stacktop!=0);
#endif


    plPriv::hashStr_t hashedFrame = 0;
    if(isCoroutine) {
        hashedFrame = pyHashPointer(frame);
        // Set the worker name, if not already done
        if(!gOsThread.isWorkerNameDeclared) {
            gOsThread.isWorkerNameDeclared = 1;
            plDetachVirtualThread(false);
            gGlobMutex.lock();
            gAsyncWorkerCount += 1;
            int count = gAsyncWorkerCount;
            gGlobMutex.unlock();
            if(count==1) snprintf(tmpStr, sizeof(tmpStr), "Workers/Async worker");
            else         snprintf(tmpStr, sizeof(tmpStr), "Workers/Async worker %d", count);
            plDeclareThreadDyn(tmpStr);
        }

        // Coroutine switch?
        if(isEnter && gOsThread.currentCoroutineFrame==0) {
            plAssert(isEnter);
            plAssert(plPriv::threadCtx.id==plPriv::threadCtx.realId);
            isCoroutineNew = plAttachVirtualThread((uint32_t)hashedFrame);
            gOsThread.currentCoroutineFrame = frame;
        }

        // Coroutine resumed? If yes, it shall be transparent, so no log
        int wasSuspended = 0;
        if(gSuspendedFrames.find(hashedFrame, wasSuspended) && wasSuspended) {
            gSuspendedFrames.replace(hashedFrame, 0);
            return; // Not a real entering, just a resuming
        }
    }

    if(plPriv::threadCtx.id==0xFFFFFFFF) plPriv::getThreadId();
    pyCommonThreadCtx_t* pctc = (plPriv::threadCtx.id<PL_MAX_THREAD_QTY)? &gThreads[plPriv::threadCtx.id] : 0;


    // Get information on the function
    // ================================

    plPriv::hashStr_t palanteerStrHash = 0;
    plPriv::hashStr_t filenameHash = 0;
    plPriv::hashStr_t nameHash = 0;
    const char*       filename = 0;
    const char*       name     = 0;
    int               lineNbr  = 0;
    int               isNewFilterOut = 0;  // 0: not filtered    1: filter also below   2: only current level is filtered

    if(calledFromC) { // C function
        // Do not get any info if it is filtered. The stack level is enough
        if(pctc && pctc->stackDepth<pctc->filterOutDepth) {
            PyCFunctionObject* cfn               = (PyCFunctionObject*)arg;
            plPriv::hashStr_t objectFilenameHash = pyHashPointer(cfn->m_ml); // Using cfn is ambiguous, cfn->m_ml is not
            plPriv::hashStr_t objectNameHash     = pyHashPointer(cfn->m_ml->ml_name);

            // Module/filename
            gGlobMutex.lock(); // Note: We must not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
            gFilterOutObject.find(objectNameHash, isNewFilterOut);
            bool isObjectFoundInCache = (isNewFilterOut==0) && gHashStrLookup.find(objectFilenameHash, palanteerStrHash);
            gGlobMutex.unlock();

            if(isNewFilterOut) {
            }
            else if(isObjectFoundInCache) {
                filenameHash = palanteerStrHash; // And filename is still zero (already known by Palanteer, so similar to a static string)
            }
            else {
                // Get module name
                PyObject* module = cfn->m_module;
                if(!module) filename = "builtins";
                else if(PyUnicode_Check(module)) filename = PyUnicode_AsUTF8(module);
                else if(PyModule_Check(module)) {
                    filename = PyModule_GetName(module);
                    if(!filename) filename = "<unknown module>";
                }
                else filename = PyUnicode_AsUTF8(PyObject_Str(module));

                // Update the lookup
                palanteerStrHash = plPriv::hashString(filename);
                gGlobMutex.lock();
                gHashStrLookup.insert(objectFilenameHash, palanteerStrHash); // We let the Palanteer filenameHash set to zero because it is a dynamic string
                gGlobMutex.unlock();
                Py_INCREF(arg);
            }

            // Check if the module name is filtered
            if(!isNewFilterOut) gFilterOutClassName.find(palanteerStrHash, isNewFilterOut);

            // Function/name
            gGlobMutex.lock(); // Note: We must not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
            isObjectFoundInCache = gHashStrLookup.find(objectNameHash, palanteerStrHash);
            gGlobMutex.unlock();

            if(isObjectFoundInCache) {
                nameHash = palanteerStrHash; // And name is still zero (already known by Palanteer)
            }
            else {
                name = cfn->m_ml->ml_name; // And nameHash is still zero (dynamic string for Palanteer)
                palanteerStrHash = plPriv::hashString(name);
                gGlobMutex.lock();
                if(isNewFilterOut) gFilterOutObject.insert(objectNameHash, isNewFilterOut);
                else               gHashStrLookup  .insert(objectNameHash, palanteerStrHash); // We let the Palanteer nameHash set to zero because it is a dynamic string
                gGlobMutex.unlock();
                Py_INCREF(arg);
            }
        } // End of the function info retrieval (skipped if filtered)
    } // End of case of C function


    // Python function
    else {
        // Do not get any info if it is filtered. The stack level is enough
        if(pctc && pctc->stackDepth<pctc->filterOutDepth) {
            PyCodeObject*     objectCode         = frame->f_code;
            plPriv::hashStr_t objectNameHash     = pyHashPointer(objectCode);
            plPriv::hashStr_t objectFilenameHash = pyHashPointer(objectCode->co_filename);
            lineNbr                              = objectCode->co_firstlineno;

            // Note: We shall not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
            gGlobMutex.lock();
            gFilterOutObject.find(objectNameHash, isNewFilterOut);
            bool isObjectFoundInCache = (isNewFilterOut==0) && gHashStrLookup.find(objectNameHash, palanteerStrHash);
            gGlobMutex.unlock();

            // Function/name
            if(isNewFilterOut) {
            }
            else if(isObjectFoundInCache) {
                nameHash = palanteerStrHash; // And name is still zero (already known by Palanteer)
            }
            else {
                PyFrame_FastToLocals(frame); // Update locals for access
                // Try getting the class name only if the first argument is "self"
                if(objectCode->co_argcount && !strcmp(PyUnicode_AsUTF8(PyTuple_GET_ITEM(objectCode->co_varnames, 0)), "self")) {
                    PyObject* locals = frame->f_locals;
                    if(locals) {
                        PyObject* self = PyDict_GetItemString(locals, "self");
                        if(self) {
                            PyObject* classObj = PyObject_GetAttrString(self, "__class__");
                            if(classObj) {
                                PyObject* classNameObj = PyObject_GetAttrString(classObj, "__name__");
                                if(classNameObj) {  // The name is "<class>.<symbol>"
                                    const char* classStr = PyUnicode_AsUTF8(classNameObj);
                                    // Check if this class is filtered out
                                    gFilterOutClassName.find(plPriv::hashString(classStr), isNewFilterOut);
                                    // Build the name
                                    snprintf(tmpStr, sizeof(tmpStr), "%s.%s", classStr, PyUnicode_AsUTF8(objectCode->co_name));
                                    name = tmpStr;  // Note: tmpStr shall not be reused before logging the enter/leave event
                                    Py_DECREF(classNameObj);
                                }
                                Py_DECREF(classObj);
                            }
                        }
                    }
                }

                // If the class name was not found, then we just use "<symbol>"
                if(!name) name = PyUnicode_AsUTF8(objectCode->co_name); // And nameHash is still zero (dynamic string for Palanteer)
                palanteerStrHash = plPriv::hashString(name);

                // Check if the function shall be filtered out
                if(!isNewFilterOut) {
                    gFilterOutFunctionName.find(palanteerStrHash, isNewFilterOut);
                }

                // Update the lookups
                gGlobMutex.lock();
                if(isNewFilterOut) gFilterOutObject.insert(objectNameHash, isNewFilterOut);
                else               gHashStrLookup  .insert(objectNameHash, palanteerStrHash); // We let the Palanteer nameHash set to zero because it is a dynamic string
                gGlobMutex.unlock();
                Py_INCREF(objectCode);
            }

            // Module/filename
            gGlobMutex.lock(); // Note: We shall not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
            isObjectFoundInCache = gHashStrLookup.find(objectFilenameHash, palanteerStrHash);
            gGlobMutex.unlock();

            if(isObjectFoundInCache) {
                filenameHash = palanteerStrHash; // And filename is still zero (already known by Palanteer, so similar to a static string)
            } else {
                filename         = PyUnicode_AsUTF8(objectCode->co_filename);
                palanteerStrHash = plPriv::hashString(filename);
                gGlobMutex.lock();
                if(!isNewFilterOut) {
                    gHashStrLookup.insert(objectFilenameHash, palanteerStrHash); // We let the Palanteer filenameHash set to zero because it is a dynamic string
                }
                gGlobMutex.unlock();
                Py_INCREF(objectCode);
                Py_INCREF(objectCode->co_filename);
            }
        }  // End of the function info retrieval (skipped if filtered)

        // Update of the stack per thread (used by the manual calls to get location info)
        if(pctc) {
            if(isEnter) {
                // Update the filtering depth (before updating the stack depth)
                if(isNewFilterOut==1 && pctc && pctc->filterOutDepth > pctc->stackDepth) {
                    pctc->filterOutDepth = pctc->stackDepth;
                }

                // Update the stack and save scope information
                plAssert(pctc->stackDepth<STACK_MAX_DEPTH);
                pctc->stack[pctc->stackDepth].filenameHash = palanteerStrHash;  // Null in case of filtering. Not important because filtered so not used
                pctc->stack[pctc->stackDepth].lineNbr      = lineNbr;
                if(pctc->stackDepth<STACK_MAX_DEPTH-1) pctc->stackDepth++;
            }
            else if(!isCoroutineSuspended && pctc->stackDepth>0) {
                // Update the stack
                pctc->stackDepth--;
            }
        }

    } // End of case of Python function


    // Log the enter/leave of the function
    // ===================================

    if(!isCoroutineSuspended && pctc && pctc->stackDepth<pctc->filterOutDepth && isNewFilterOut==0) {
        // Log the Palanteer event
        plAssert(filename || filenameHash);
        plAssert(name     || nameHash, isNewFilterOut, isEnter, calledFromC, pctc->stackDepth, pctc->filterOutDepth);
        const char* sentFilename = filename? plPriv::getDynString(filename) : 0;
        const char* sentName     = name?     plPriv::getDynString(name) : 0;
        uint32_t bi = plPriv::globalCtx.bankAndIndex.fetch_add(1);
        plPriv::EventInt& e = plPriv::eventLogBase(bi, filenameHash, nameHash, sentFilename, sentName, lineNbr,
                                                   PL_FLAG_TYPE_DATA_TIMESTAMP | (isEnter? PL_FLAG_SCOPE_BEGIN : PL_FLAG_SCOPE_END));
        e.vU64  = PL_GET_CLOCK_TICK_FUNC();
        e.magic = bi;
        plPriv::eventCheckOverflow(bi);
    }

    // Reset the filtering rule if the stack depth is back to the initial filtering depth
    if(pctc && !isEnter && pctc->filterOutDepth!=STACK_MAX_DEPTH && pctc->stackDepth<=pctc->filterOutDepth) {
        pctc->filterOutDepth = STACK_MAX_DEPTH;
    }

    // Co-routine management (second part)
    // ==================================

    if(isCoroutine) {

        // Automatically set the name of the new coroutine, based on the current function name (async function)
        if(isCoroutineNew && (nameHash || name)) {
            // Get the coroutine name structure (to keep track of the multiple same name, which is a probable case)
            int coroutineNameIdx = -1;
            plPriv::hashStr_t coroutineNameHash = nameHash? nameHash : plPriv::hashString(name);
            gGlobMutex.lock();
            if(!gCoroutineNameToIdx.find(coroutineNameHash, coroutineNameIdx)) {
                // Create a new entry
                if(name && gCoroutineNames.size()<PL_MAX_THREAD_QTY) {
                    coroutineNameIdx = (int)gCoroutineNames.size();
                    gCoroutineNameToIdx.insert(coroutineNameHash, coroutineNameIdx);
                    gCoroutineNames.push_back(CoroutineNaming());
                    int minSize = (int)(strlen(name)+1);
                    if(minSize>PL_DYN_STRING_MAX_SIZE) minSize = PL_DYN_STRING_MAX_SIZE;
                    memcpy(gCoroutineNames.back().name, name, minSize);
                    gCoroutineNames.back().name[PL_DYN_STRING_MAX_SIZE-1] = 0;
                }
            }

            // Declare the virtual thread name
            if(coroutineNameIdx>=0) {
                CoroutineNaming& cn = gCoroutineNames[coroutineNameIdx];
                cn.namingCount += 1;
                if(cn.namingCount==1) snprintf(tmpStr, sizeof(tmpStr), "Async/%s", cn.name);
                else                  snprintf(tmpStr, sizeof(tmpStr), "Async/%s %d", cn.name, cn.namingCount);
                plDeclareVirtualThread((uint32_t)hashedFrame, tmpStr);

                // Log the previously skipped "begin" event on the worker thread (because name was not set)
                plPriv::ThreadContext_t* tCtx = &plPriv::threadCtx;
                uint32_t vThreadId = tCtx->id;
                if(vThreadId<PL_MAX_THREAD_QTY && PL_IS_ENABLED_()) {
                    plPriv::ThreadInfo_t& ti = plPriv::globalCtx.threadInfos[vThreadId];
                    if(ti.nameHash!=0 && !ti.isBeginSent) {
                        tCtx->id = tCtx->realId; // Switch to the OS thread
                        plPriv::eventLogRaw(PL_STRINGHASH(PL_BASEFILENAME), ti.nameHash, PL_EXTERNAL_STRINGS?0:PL_BASEFILENAME, 0, 0, 0,
                                            PL_FLAG_SCOPE_BEGIN | PL_FLAG_TYPE_DATA_TIMESTAMP, PL_GET_CLOCK_TICK_FUNC());
                        ti.isBeginSent = true;
                        tCtx->id = vThreadId;
                    }
                }

            }
            gGlobMutex.unlock();
        }

        if(isCoroutineSuspended) {
            // Flag this frame as suspended
            if(!gSuspendedFrames.replace(hashedFrame, 1)) {
                gSuspendedFrames.insert(hashedFrame, 1);
            }
        }
        // Is it the coroutine top frame?
        if(!isEnter && gOsThread.currentCoroutineFrame==frame) {
            // Detach the coroutine
            gOsThread.currentCoroutineFrame = 0;
            plAssert(plPriv::threadCtx.id!=plPriv::threadCtx.realId);
            plDetachVirtualThread(isCoroutineSuspended);
        }
    } // End of coroutine management
}


// Python profiling/tracing hooks (entry for the automatic instrumentation)
// ========================================================================

static int
profileCallback(PyObject* self, PyFrameObject* frame, int what, PyObject* arg)
{
    if(!PL_IS_ENABLED_())  return 0;

    // Save the error state (this function shall be "transparent")
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);

    // Switch on the type (can be only call, return, c_call c_return and c_exception)
    switch(what) {
    case PyTrace_CALL:
        logFunctionEvent(self, frame, arg, true,  false); break;
    case PyTrace_RETURN:
        logFunctionEvent(self, frame, arg, false, false); break;

    // C calls below are generated only in case of using "setprofile"
    case PyTrace_C_CALL:
        if(gWithCCalls && PyCFunction_Check(arg)) { logFunctionEvent(self, frame, arg, true , true); } break;
    case PyTrace_C_RETURN:
        if(gWithCCalls && PyCFunction_Check(arg)) { logFunctionEvent(self, frame, arg, false, true); } break;
    case PyTrace_C_EXCEPTION:
        if(gWithCCalls && PyCFunction_Check(arg)) { logFunctionEvent(self, frame, arg, false, true); } break; // They are independent of C_RETURN
    default: break;
    }

    // Restore the error state
    if(ptype) PyErr_Restore(ptype, pvalue, ptraceback);
    return 0;
}


static int
traceCallback(PyObject* Py_UNUSED(self), PyFrameObject* frame, int what, PyObject* arg)
{
    // Generated event can be only call, return, line or exception.
    // The function leave/enter are handled by the "profile" callback above.
    // Here, we just want the additional "exception" info, as it is an important part in the Python language
    // The "line" info is overkill for profiling, it is skipped also.
    if(what!=PyTrace_EXCEPTION) return 0;
    if(!PL_IS_ENABLED_())  return 0;
    if(plPriv::threadCtx.id>=PL_MAX_THREAD_QTY) return 0;

    // Save the error state (this function shall be "transparent")
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);

    // Only the bottom of the exception stack is processed
    pyCommonThreadCtx_t& ctc = gThreads[plPriv::threadCtx.id];
    if(ctc.nextExceptionFrame!=frame && ctc.stackDepth<ctc.filterOutDepth) {
        // Log a marker with the line number (function is the current scope) and the exception text
        PyObject* exceptionRepr = PyObject_Repr(PyTuple_GET_ITEM(arg, 1)); // Representation of the exception "value"
        char msg[256];
        snprintf(msg, sizeof(msg), "line %d: %s", PyFrame_GetLineNumber(frame), PyUnicode_AsUTF8(exceptionRepr));
        plPriv::hashStr_t palanteerCategoryStrHash, palanteerMsgStrHash, strHash;

        gGlobMutex.lock();
        CACHE_STRING("Exception", palanteerCategoryStrHash);
        CACHE_STRING(msg,         palanteerMsgStrHash);
        gGlobMutex.unlock();
        pyEventLogRaw(palanteerMsgStrHash, palanteerCategoryStrHash, msg, "Exception", 0, PL_FLAG_TYPE_MARKER, PL_GET_CLOCK_TICK_FUNC());
        Py_XDECREF(exceptionRepr);
    }

    // Store upper level so that we skip it
    ctc.nextExceptionFrame = frame->f_back;
    gOsThread.isBootstrap = 0;

    // Restore the error state
    if(ptype) PyErr_Restore(ptype, pvalue, ptraceback);
    return 0;
}


// Manual instrumentation
// ======================

static PyObject*
pyPlDeclareThread(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;

    // Parse arguments
    char* name = 0;
    if(!PyArg_ParseTuple(args, "s", &name)) {
        PyErr_SetString(PyExc_TypeError, "Unable to decode the parameter. A string is expected.");
        return 0;
    }

    plPriv::hashStr_t palanteerStrHash, strHash;
    gGlobMutex.lock();
    CACHE_STRING(name, palanteerStrHash);
    gGlobMutex.unlock();

    // Log
    pyEventLogRaw(plPriv::hashString(""), palanteerStrHash, 0, name, 0, PL_FLAG_TYPE_THREADNAME, 0);

    Py_RETURN_NONE;
}


static PyObject*
pyPlData(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;
    if(plPriv::threadCtx.id>=PL_MAX_THREAD_QTY) Py_RETURN_NONE;
    pyCommonThreadCtx_t& ctc = gThreads[plPriv::threadCtx.id];
    if(ctc.stackDepth==0) {
        PyErr_SetString(PyExc_TypeError, "Data must be logged inside a scope (root here). Either move in a function or use plBegin/plEnd to create a root scope.");
        return 0;
    }
    if(ctc.stackDepth>=ctc.filterOutDepth) Py_RETURN_NONE; // Filtered
    ThreadStackElem_t& tc = ctc.stack[ctc.stackDepth-1];

    // Parse arguments
    char* name = 0;
    PyObject* dataObj = 0;
    if(!PyArg_ParseTuple(args, "sO", &name, &dataObj)) { Py_RETURN_NONE; }

    // Get the data name
    plPriv::hashStr_t palanteerStrHash, strHash;
    gGlobMutex.lock();
    CACHE_STRING(name, palanteerStrHash);
    gGlobMutex.unlock();

    // Integer case
    uint64_t valueDataU64;
    if(PyLong_Check(dataObj)) {
        int isOverflow = 0;
        valueDataU64 = (uint64_t)PyLong_AsLongLongAndOverflow(dataObj, &isOverflow);  // If overflow, we try the unsigned version (1 more bit available)
        if(!isOverflow && !PyErr_Occurred()) {
            pyEventLogRaw(tc.filenameHash, palanteerStrHash, 0, name,
                          tc.lineNbr, PL_FLAG_TYPE_DATA_S64, valueDataU64);
        } else {
            valueDataU64 = PyLong_AsUnsignedLongLongMask(dataObj);  // "Mask" => if overflow, use the modulo on 64 bits
            if(!PyErr_Occurred()) {
                pyEventLogRaw(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr, PL_FLAG_TYPE_DATA_U64, valueDataU64);
            }
        }
    }

    // Float case
    else if(PyFloat_Check(dataObj)) {
        double v     = PyFloat_AS_DOUBLE(dataObj);
        char* tmp    = (char*)(&v);
        valueDataU64 = *((uint64_t*)tmp);
        if(!PyErr_Occurred()) {
            pyEventLogRaw(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr, PL_FLAG_TYPE_DATA_DOUBLE, valueDataU64);
        }
    }

    // String case
    else if(PyUnicode_Check(dataObj)) {
        plPriv::hashStr_t palanteerValueStrHash;
        const char* valueStr = PyUnicode_AsUTF8(dataObj);
        gGlobMutex.lock();
        CACHE_STRING(valueStr, palanteerValueStrHash);
        gGlobMutex.unlock();
        pyEventLogRawString(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr, palanteerValueStrHash, valueStr);
    }

    // None case: turn it into an empty string
    else if(dataObj==Py_None) {
        pyEventLogRawString(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr, plPriv::hashString(""), "");
    }

    // Else unknown type, no logging

    Py_RETURN_NONE;
}


static PyObject*
pyPlMarker(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;
    if(plPriv::threadCtx.id>=PL_MAX_THREAD_QTY) Py_RETURN_NONE;
    pyCommonThreadCtx_t& ctc = gThreads[plPriv::threadCtx.id];
    if(ctc.stackDepth>=ctc.filterOutDepth) Py_RETURN_NONE; // Filtered

    // Parse arguments
    char* category = 0, *msg = 0;
    if(!PyArg_ParseTuple(args, "ss", &category, &msg)) { Py_RETURN_NONE; }

    plPriv::hashStr_t palanteerCategoryStrHash, palanteerMsgStrHash, strHash;
    gGlobMutex.lock();
    CACHE_STRING(category, palanteerCategoryStrHash);
    CACHE_STRING(msg,      palanteerMsgStrHash);
    gGlobMutex.unlock();

    pyEventLogRaw(palanteerMsgStrHash, palanteerCategoryStrHash, msg, category, 0, PL_FLAG_TYPE_MARKER, PL_GET_CLOCK_TICK_FUNC());

    Py_RETURN_NONE;
}


static PyObject*
pyPlLockWait(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;
    if(plPriv::threadCtx.id>=PL_MAX_THREAD_QTY) Py_RETURN_NONE;
    pyCommonThreadCtx_t& ctc = gThreads[plPriv::threadCtx.id];

    // Parse arguments
    char* name = 0;
    if(!PyArg_ParseTuple(args, "s", &name)) { Py_RETURN_NONE; }

    if(gWithFunctions && ctc.stackDepth!=0 && ctc.stackDepth<ctc.filterOutDepth) {
        plPriv::hashStr_t palanteerStrHash, strHash;
        gGlobMutex.lock();
        CACHE_STRING(name, palanteerStrHash);
        gGlobMutex.unlock();
        ThreadStackElem_t& tc = ctc.stack[ctc.stackDepth-1];
        pyEventLogRaw(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr, PL_FLAG_SCOPE_BEGIN | PL_FLAG_TYPE_LOCK_WAIT, PL_GET_CLOCK_TICK_FUNC());
    }
    else {
        int               lineNbr  = 0;
        const char*       filename = 0;
        plPriv::hashStr_t objectFilenameHash=0, palanteerFilenameStrHash=0, palanteerStrHash=0;
        pyGetNameFilenameLineNbr(name, filename, lineNbr, objectFilenameHash, palanteerFilenameStrHash, palanteerStrHash);
        pyEventLogRaw(palanteerFilenameStrHash, palanteerStrHash, filename, name, lineNbr,
                      PL_FLAG_SCOPE_BEGIN | PL_FLAG_TYPE_LOCK_WAIT, PL_GET_CLOCK_TICK_FUNC());
    }


    Py_RETURN_NONE;
}


static PyObject*
pyPlLockState(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;
    if(plPriv::threadCtx.id>=PL_MAX_THREAD_QTY) Py_RETURN_NONE;
    pyCommonThreadCtx_t& ctc = gThreads[plPriv::threadCtx.id];

    // Parse arguments
    char* name  = 0;
    int   state = 0;
    if(!PyArg_ParseTuple(args, "sp", &name, &state)) { Py_RETURN_NONE; }

    if(gWithFunctions && ctc.stackDepth!=0 && ctc.stackDepth<ctc.filterOutDepth) {
        plPriv::hashStr_t palanteerStrHash, strHash;
        gGlobMutex.lock();
        CACHE_STRING(name, palanteerStrHash);
        gGlobMutex.unlock();
        ThreadStackElem_t& tc = ctc.stack[ctc.stackDepth-1];
        pyEventLogRaw(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr,
                      state? PL_FLAG_TYPE_LOCK_ACQUIRED : PL_FLAG_TYPE_LOCK_RELEASED, PL_GET_CLOCK_TICK_FUNC());
    }
    else {
        int               lineNbr  = 0;
        const char*       filename = 0;
        plPriv::hashStr_t objectFilenameHash=0, palanteerFilenameStrHash=0, palanteerStrHash=0;
        pyGetNameFilenameLineNbr(name, filename, lineNbr, objectFilenameHash, palanteerFilenameStrHash, palanteerStrHash);
        pyEventLogRaw(palanteerFilenameStrHash, palanteerStrHash, filename, name, lineNbr,
                      state? PL_FLAG_TYPE_LOCK_ACQUIRED : PL_FLAG_TYPE_LOCK_RELEASED, PL_GET_CLOCK_TICK_FUNC());
    }

    Py_RETURN_NONE;
}


static PyObject*
pyPlLockNotify(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;
    if(plPriv::threadCtx.id>=PL_MAX_THREAD_QTY) Py_RETURN_NONE;
    pyCommonThreadCtx_t& ctc = gThreads[plPriv::threadCtx.id];

    // Parse arguments
    char* name = 0;
    if(!PyArg_ParseTuple(args, "s", &name)) { Py_RETURN_NONE; }

    if(gWithFunctions && ctc.stackDepth!=0 && ctc.stackDepth<ctc.filterOutDepth) {
        plPriv::hashStr_t palanteerStrHash, strHash;
        gGlobMutex.lock();
        CACHE_STRING(name, palanteerStrHash);
        gGlobMutex.unlock();
        ThreadStackElem_t& tc = ctc.stack[ctc.stackDepth-1];
        pyEventLogRaw(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr, PL_FLAG_TYPE_LOCK_NOTIFIED, PL_GET_CLOCK_TICK_FUNC());
    }
    else {
        int               lineNbr  = 0;
        const char*       filename = 0;
        plPriv::hashStr_t objectFilenameHash=0, palanteerFilenameStrHash=0, palanteerStrHash=0;
        pyGetNameFilenameLineNbr(name, filename, lineNbr, objectFilenameHash, palanteerFilenameStrHash, palanteerStrHash);
        pyEventLogRaw(palanteerFilenameStrHash, palanteerStrHash, filename, name, lineNbr, PL_FLAG_TYPE_LOCK_NOTIFIED, PL_GET_CLOCK_TICK_FUNC());
    }

    Py_RETURN_NONE;
}


static PyObject*
pyPlBegin(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;
    if(plPriv::threadCtx.id>=PL_MAX_THREAD_QTY) Py_RETURN_NONE;
    pyCommonThreadCtx_t& ctc = gThreads[plPriv::threadCtx.id];
    if(ctc.stackDepth>=ctc.filterOutDepth) Py_RETURN_NONE; // Filtered

    // Parse arguments
    char* name = 0;
    if(!PyArg_ParseTuple(args, "s", &name)) {
        PyErr_SetString(PyExc_TypeError, "Unable to decode the parameter. A string is expected.");
        return 0;
    }

    // Name hash, module/filename and line number
    int               lineNbr  = 0;
    const char*       filename = 0;
    plPriv::hashStr_t objectFilenameHash=0, palanteerFilenameStrHash=0, palanteerStrHash=0;
    pyGetNameFilenameLineNbr(name, filename, lineNbr, objectFilenameHash, palanteerFilenameStrHash, palanteerStrHash);

    // Log
    pyEventLogRaw(palanteerFilenameStrHash, palanteerStrHash, filename, name, lineNbr,
                  PL_FLAG_SCOPE_BEGIN | PL_FLAG_TYPE_DATA_TIMESTAMP, PL_GET_CLOCK_TICK_FUNC());

    // Update the stack
    plAssert(ctc.stackDepth<STACK_MAX_DEPTH);
    ctc.stack[ctc.stackDepth].filenameHash = palanteerFilenameStrHash? palanteerFilenameStrHash : plPriv::hashString(filename); // Save scope information
    ctc.stack[ctc.stackDepth].lineNbr = lineNbr; // Save scope information
    if(ctc.stackDepth<STACK_MAX_DEPTH-1) ctc.stackDepth++;
    Py_RETURN_NONE;
}


static PyObject*
pyPlEnd(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;
    if(plPriv::threadCtx.id>=PL_MAX_THREAD_QTY) Py_RETURN_NONE;
    pyCommonThreadCtx_t& ctc = gThreads[plPriv::threadCtx.id];
    if(ctc.stackDepth>=ctc.filterOutDepth) Py_RETURN_NONE; // Filtered

    if(ctc.stackDepth<=0) {
        PyErr_SetString(PyExc_TypeError, "plEnd is called at the scope root. Check that all plBegin get a corresponding plEnd.");
        return 0;
    }
    ThreadStackElem_t& tc = ctc.stack[ctc.stackDepth-1];

    // Parse arguments
    char* name = (char*)"";
    if(!PyArg_ParseTuple(args, "|s", &name)) {
        PyErr_SetString(PyExc_TypeError, "Unable to decode the parameter. A string is expected.");
        return 0;
    }

    plPriv::hashStr_t palanteerStrHash, strHash;
    gGlobMutex.lock();
    CACHE_STRING(name, palanteerStrHash);
    gGlobMutex.unlock();

    // Log
    pyEventLogRaw(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr, PL_FLAG_SCOPE_END | PL_FLAG_TYPE_DATA_TIMESTAMP, PL_GET_CLOCK_TICK_FUNC());

    // Update the stack
    ctc.stackDepth--;
    if(ctc.filterOutDepth!=STACK_MAX_DEPTH && ctc.filterOutDepth>ctc.stackDepth) ctc.filterOutDepth = STACK_MAX_DEPTH;

    Py_RETURN_NONE;
}


// Profiler
// ========

static PyObject*
_profiling_bootstrap_callback(PyObject* self, PyObject* args)
{
    // Decode the standard parameters
    PyFrameObject* frame;
    PyObject*      event;
    PyObject*      arg;
    if(!PyArg_ParseTuple(args, "OOO", &frame, &event, &arg)) return 0;

    // Update the profiling event callback
    PyThreadState* threadState = PyThreadState_GET();
    if(threadState->c_profilefunc!=profileCallback) {
        // Replace callbacks. This function is called so it means that one of the two is enabled.
        threadState->USE_TRACING_ACCESS = 1;
        threadState->c_profilefunc      = gWithFunctions ? profileCallback : 0;
        threadState->c_tracefunc        = gWithExceptions? traceCallback   : 0;
    }

    if(gWithFunctions) {
        // Re-route this "profile" event to the C-callback, which takes an enum for the event type (here, it is a string)
        const char* eventTypeStr = PyUnicode_AsUTF8(event);
        if      (strcmp("call",        eventTypeStr)==0) profileCallback(self, frame, PyTrace_CALL, arg);
        else if (strcmp("return",      eventTypeStr)==0) profileCallback(self, frame, PyTrace_RETURN, arg);
        else if (strcmp("c_call",      eventTypeStr)==0) profileCallback(self, frame, PyTrace_C_CALL, arg);
        else if (strcmp("c_return",    eventTypeStr)==0) profileCallback(self, frame, PyTrace_C_RETURN, arg);
        else if (strcmp("c_exception", eventTypeStr)==0) profileCallback(self, frame, PyTrace_C_EXCEPTION, arg);
    }

    Py_RETURN_NONE;
}


// CLIs
// ======

void
genericCliHandler(plCliIo& cio)
{
    // We are in a non-python thread
    PyGILState_STATE gstate  = PyGILState_Ensure();

    // Get back the name and parameters from cio to build the Python call
    PyObject* cliHandlerObj=0;
    gGlobMutex.lock();
    bool status = gCliHandlerLookup.find(cio.getCliNameHash(), cliHandlerObj);
    gGlobMutex.unlock();
    plAssert(status);
    PyObject* argTuple = PyTuple_New(cio.getParamQty());
    for(int i=0; i<cio.getParamQty(); ++i) {
        PyObject* paramObj;
        if     (cio.isParamInt(i))   paramObj = PyLong_FromUnsignedLongLong(cio.getParamInt(i));
        else if(cio.isParamFloat(i)) paramObj = PyFloat_FromDouble(cio.getParamFloat(i));
        else                         paramObj = PyUnicode_FromString(cio.getParamString(i));
        PyTuple_SET_ITEM(argTuple, i, paramObj);
    }

    // Call python (and handle the potential errors and exceptions)
    PyObject* answerObj = PyObject_CallObject(cliHandlerObj, argTuple);

    // Get the answer
    if(!answerObj) {  // Error: no answer object returned so an exception occured
        PyObject *ptype, *pvalue, *ptraceback;
        PyErr_Fetch(&ptype, &pvalue, &ptraceback);
        cio.setErrorState("**Python CLI implementation exception**: %s", PyUnicode_AsUTF8(pvalue));
    }
    else if(PyLong_Check(answerObj)) { // Single integer returned = the status
        if(PyLong_AsLong(answerObj)) cio.setErrorState();
    }
    else if(!PyTuple_Check(answerObj)) { // Error: something other than a tuple returned
        cio.setErrorState("**Python CLI implementation error**: The CLI handler did not return a tuple. Expected is (status integer, answer string). Null status means 'OK'.");
    }
    else {
        int size = (int)PyTuple_Size(answerObj);
        if(size<=0 || size>2) {
            cio.setErrorState("**Python CLI implementation error**: The CLI handler returned a tuple with incorrect size. Expected is (status integer, answer string). Null status means 'OK'.");
        }
        else if(!PyLong_Check(PyTuple_GET_ITEM(answerObj, 0))) {
            cio.setErrorState("**Python CLI implementation error**: The CLI handler returned a tuple with incorrect status type. Expected is (status integer, answer string). Null status means 'OK'.");
        }
        else {
            if(size==2 && !PyUnicode_Check(PyTuple_GET_ITEM(answerObj, 1))) {
                cio.setErrorState("**Python CLI implementation error**: The CLI handler returned a tuple with incorrect answer string type. Expected is (status integer, answer string). Null status means 'OK'.");
            }
            else {
                // Non zero status means failure
                if(PyLong_AsLong(PyTuple_GET_ITEM(answerObj, 0))) cio.setErrorState();
                // Set the answer message, if any
                cio.addToResponse("%s", (size==2)? PyUnicode_AsUTF8(PyTuple_GET_ITEM(answerObj, 1)) : "");
            }
        }
    }

    // Cleanup
    Py_XDECREF(answerObj);
    Py_DECREF(argTuple);
    PyGILState_Release(gstate);
}


static PyObject*
pyPlRegisterCli(PyObject* Py_UNUSED(self), PyObject* args)
{
    // Parse arguments
    // #define plRegisterCli(handler, name, specParams, description)
    PyObject* cliHandlerObj=0;
    char *name=0, *specParams=0, *description=0;
    if(!PyArg_ParseTuple(args, "Osss", &cliHandlerObj, &name, &specParams, &description)) {
        PyErr_SetString(PyExc_TypeError, "Unable to decode the registration parameters. Expected is: 'function, cli name string, spec param string, description string'.");
        return 0;
    }
    if(!PyCallable_Check(cliHandlerObj)) {
        PyErr_SetString(PyExc_TypeError, "First parameter shall be a function");
        return 0;
    }
    Py_INCREF(cliHandlerObj);

    // Register the CLI
    gGlobMutex.lock();
    gCliHandlerLookup.insert(plPriv::hashString(name), cliHandlerObj);
    gGlobMutex.unlock();
    plPriv::registerCli(genericCliHandler, name, specParams, description,
                        plPriv::hashString(name), plPriv::hashString(specParams), plPriv::hashString(description));
    Py_RETURN_NONE;
}


static PyObject*
pyPlFreezePoint(PyObject* Py_UNUSED(self), PyObject* args)
{
    // Release the GIL
    Py_BEGIN_ALLOW_THREADS
    plFreezePoint();
    Py_END_ALLOW_THREADS

    Py_RETURN_NONE;
}


// Memory wrappers
// ===============

void*
pyWrapMalloc(void *ctx, size_t size)
{
    void* ptr = gOldAllocatorRaw.malloc(ctx, size);
    if(PL_IS_ENABLED_()) plPriv::eventLogAlloc(ptr, (uint32_t)size);
    return ptr;
}


void*
pyWrapCalloc(void *ctx, size_t nelem, size_t elsize)
{
    void* ptr = gOldAllocatorRaw.calloc(ctx, nelem, elsize);
    if(PL_IS_ENABLED_()) plPriv::eventLogAlloc(ptr, (uint32_t)(nelem*elsize));
    return ptr;
}


void*
pyWrapRealloc(void *ctx, void *ptr, size_t new_size)
{
    if(PL_IS_ENABLED_()) plPriv::eventLogDealloc(ptr);
    ptr = gOldAllocatorRaw.realloc(ctx, ptr, new_size);
    if(PL_IS_ENABLED_()) plPriv::eventLogAlloc(ptr, (uint32_t)new_size);
    return ptr;
}


void
pyWrapFree(void *ctx, void *ptr)
{
    if(PL_IS_ENABLED_()) plPriv::eventLogDealloc(ptr);
    gOldAllocatorRaw.free(ctx, ptr);
}


// Start and stop profiling
// =========================

static PyObject*
_profiling_start(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(gIsEnabled) {
        Py_RETURN_NONE;
    }
    gIsEnabled = true;
    gCoroutineNames.reserve(PL_MAX_THREAD_QTY);

    // Get the parameters
    char* appName=0, *buildName=0, *recordFilename=0, *server_address=0;
    int server_port=0, doWaitForServerConnection=0, withFunctions=0, withExceptions=0, withMemory=0, withCCalls=0;
    if(!PyArg_ParseTuple(args, "szzsiiiiii", &appName, &recordFilename, &buildName,
                         &server_address, &server_port, &doWaitForServerConnection,
                         &withFunctions, &withExceptions, &withMemory, &withCCalls)) {
        CheckForPyError();
        PyErr_SetString(PyExc_TypeError, "Unable to decode the parameters.");
        return 0;
    }

    // Store the config
    gWithFunctions  = withFunctions;
    gWithExceptions = withExceptions;
    gWithMemory     = withMemory;
    gWithCCalls     = withCCalls;

    if(gWithMemory) {
        // Hook the "raw" memory allocator
        PyMem_GetAllocator(PYMEM_DOMAIN_RAW, &gOldAllocatorRaw);
        gNewAllocatorRaw.ctx     = gOldAllocatorRaw.ctx;
        gNewAllocatorRaw.malloc  = pyWrapMalloc;
        gNewAllocatorRaw.calloc  = pyWrapCalloc;
        gNewAllocatorRaw.realloc = pyWrapRealloc;
        gNewAllocatorRaw.free    = pyWrapFree;
        PyMem_SetAllocator(PYMEM_DOMAIN_RAW, &gNewAllocatorRaw);
    }

    // Fill the filtering hashsets
    gFilterOutClassName.clear();
    gFilterOutFunctionName.clear();
    int i=0; while(gFilterOutClassDb[i])            gFilterOutClassName.insert(gFilterOutClassDb[i++],               1);
    i=0;     while(gFilterOutFunctionAndBelowDb[i]) gFilterOutFunctionName.insert(gFilterOutFunctionAndBelowDb[i++], 1);
    i=0;     while(gFilterOutFunctionDb[i])         gFilterOutFunctionName.insert(gFilterOutFunctionDb[i++],         2);

    if(gWithFunctions) plPriv::implCtx.hasAutoInstrument = true;
    if(recordFilename) plSetFilename(recordFilename);
    plSetServer(server_address, server_port);
    plInitAndStart(appName, recordFilename? PL_MODE_STORE_IN_FILE: PL_MODE_CONNECTED, buildName, doWaitForServerConnection);

    if(gWithFunctions || gWithExceptions) {
        // Activate profiling on all current threads
        for(PyInterpreterState* interpState=PyInterpreterState_Head(); interpState; interpState=PyInterpreterState_Next(interpState)) {
            for(PyThreadState* threadState=PyInterpreterState_ThreadHead(interpState); threadState; threadState=threadState->next) {
                // Replace callbacks
                threadState->USE_TRACING_ACCESS = 1;
                threadState->c_profilefunc      = gWithFunctions ? profileCallback : 0;
                threadState->c_tracefunc        = gWithExceptions? traceCallback   : 0;
            }
        }
    }

    Py_RETURN_NONE;
}


static PyObject*
_profiling_stop(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!gIsEnabled) {
        Py_RETURN_NONE;
    }
    gIsEnabled = false;

    // De-activate profiling on all threads
    for(PyInterpreterState* interpState=PyInterpreterState_Head(); interpState; interpState=PyInterpreterState_Next(interpState)) {
        for(PyThreadState* threadState=PyInterpreterState_ThreadHead(interpState); threadState; threadState=threadState->next) {
            threadState->USE_TRACING_ACCESS = 0;
            threadState->c_profilefunc      = 0;
            threadState->c_tracefunc        = 0;
        }
    }
    plStopAndUninit();

    if(gWithMemory) {
        PyMem_SetAllocator(PYMEM_DOMAIN_RAW, &gOldAllocatorRaw);
    }

    Py_RETURN_NONE;
}



// Python module glue
// ==================

static PyMethodDef module_methods[] = {
    {"_profiling_start", _profiling_start, METH_VARARGS, 0},
    {"_profiling_stop",  _profiling_stop,  METH_NOARGS,  0},
    {"_profiling_bootstrap_callback", _profiling_bootstrap_callback, METH_VARARGS, 0},
    {"plDeclareThread", pyPlDeclareThread, METH_VARARGS, 0},
    {"plData",          pyPlData,          METH_VARARGS, 0},
    {"plMarker",        pyPlMarker,        METH_VARARGS, 0},
    {"plLockWait",      pyPlLockWait,      METH_VARARGS, 0},
    {"plLockState",     pyPlLockState,     METH_VARARGS, 0},
    {"plLockNotify",    pyPlLockNotify,    METH_VARARGS, 0},
    {"plBegin",         pyPlBegin,         METH_VARARGS, 0},
    {"plEnd",           pyPlEnd,           METH_VARARGS, 0},
    {"plRegisterCli",   pyPlRegisterCli,   METH_VARARGS, 0},
    {"plFreezePoint",   pyPlFreezePoint,   METH_NOARGS,  0},

    {0, 0, 0, 0} // End of list
};


PyMODINIT_FUNC
PyInit__cextension(void)
{
    static struct PyModuleDef totoModuleDef = { PyModuleDef_HEAD_INIT, "_cextension", "Palanteer Python instrumentation C extension",
                                                -1, module_methods, 0, 0, 0, 0 };
    PyObject* m = PyModule_Create(&totoModuleDef);
    return m;
}
