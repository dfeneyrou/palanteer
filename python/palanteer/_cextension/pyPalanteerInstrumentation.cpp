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

// CPython includes
#include "Python.h"
#include "frameobject.h"

// Configure and implement the C++ Palanteer instrumentation
#define USE_PL 1
#define PL_IMPLEMENTATION 1
#define PL_IMPL_OVERLOAD_NEW_DELETE 0    // Overload from a dynamic library does not work
#define PL_IMPL_MAX_CLI_QTY              1024
#define PL_IMPL_DYN_STRING_QTY           4096
#define PL_IMPL_MAX_EXPECTED_STRING_QTY 16384
#include "palanteer.h"


// Module definitions
struct ThreadStackElem_t {
    plPriv::hashStr_t filenameHash;
    plPriv::hashStr_t nameHash;
    int               lineNbr;
};
constexpr int STACK_MAX_DEPTH = 256;
struct pyThreadContext_t {
    int               isBootstrap = 1; // First events ending a scope shall be skipped, until another kind of event is received
    int               stackDepth  = 0;
    PyFrameObject*    nextExceptionFrame = 0; // To manage unwinding
    ThreadStackElem_t stack[STACK_MAX_DEPTH]; // 5 KB per thread
};

// Module state
thread_local pyThreadContext_t gThreads;
plPriv::FlatHashTable<plPriv::hashStr_t> gHashStrLookup;    // Hashed object   -> Palanteer string hash
plPriv::FlatHashTable<PyObject*>         gCliHandlerLookup; // Hashed CLI name -> Python callable object
std::mutex       gLkupMutex;
PyMemAllocatorEx gOldAllocatorRaw;
PyMemAllocatorEx gNewAllocatorRaw;
static bool gIsEnabled  = false;
static bool gWithFunctions  = false;
static bool gWithExceptions = false;
static bool gWithMemory     = false;
static bool gWithCCalls     = false;

// Constants
plPriv::hashStr_t gThisModuleNameHash = PL_STRINGHASH("palanteer._cextension");
constexpr int gFunctionsToHideQty = 4;
plPriv::hashStr_t gFunctionsToHide[gFunctionsToHideQty] = {
    PL_STRINGHASH("Thread._bootstrap"), PL_STRINGHASH("Thread._bootstrap_inner"), // Mask thread creation "leave" events, as "enter" are not seen (cf bootstrap mechanism)
    PL_STRINGHASH("_plFunctionInner"), // Mask plFunction decorator inner function
    PL_STRINGHASH("_pl_garbage_collector_notif")
};

// Python compatibility
#if PY_VERSION_HEX<0x031000B1
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
pyHashString(const void* p) {
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
    strHash    = pyHashString(s);                                       \
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
    objectFilenameHash       = pyHashString(objectCode->co_filename);

    // Note: We must not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
    gLkupMutex.lock();
    // Get the name hash (or null if it is a new string)
    plPriv::hashStr_t strHash;
    CACHE_STRING(name, palanteerNameStrHash);
    // Get the filename hash
    bool isObjectFoundInCache = gHashStrLookup.find(objectFilenameHash, palanteerFilenameStrHash);
    gLkupMutex.unlock();

    if(!isObjectFoundInCache) {
        filename = PyUnicode_AsUTF8(objectCode->co_filename);
        gLkupMutex.lock();
        gHashStrLookup.insert(objectFilenameHash, plPriv::hashString(filename)); // We let the palanteerFilenameStrHash set to zero because it is a dynamic string
        gLkupMutex.unlock();
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
    plPriv::eventLogBase(bi, filenameHash, nameHash, filenameHash? 0 : allocFileStr, nameHash? 0 : allocNameStr, lineNbr, flags).vU64 = v;
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
    plPriv::eventCheckOverflow(bi);
}


static void
logFunctionEvent(PyObject* Py_UNUSED(self), PyFrameObject* frame, PyObject* arg, bool isEnter, bool calledFromC)
{
    // Get infos from Python (filename, name, line nbr)
    char tmpStr[256];

    plPriv::hashStr_t palanteerStrHash = 0;
    plPriv::hashStr_t filenameHash = 0;
    plPriv::hashStr_t nameHash = 0;
    const char*       filename = 0;
    const char*       name     = 0;
    int               lineNbr  = 0;

    if(gThreads.isBootstrap) {
        if(!isEnter) return;      // First "leaving" events are dropped until an "enter" is found
        gThreads.isBootstrap = 0; // End of bootstrap phase
    }

    if(calledFromC) { // C function
        PyCFunctionObject* cfn               = (PyCFunctionObject*)arg;
        plPriv::hashStr_t objectFilenameHash = pyHashString(cfn->m_ml); // Using cfn is ambiguous, cfn->m_ml is not
        plPriv::hashStr_t objectNameHash     = pyHashString(cfn->m_ml->ml_name);

        // Module/filename
        gLkupMutex.lock(); // Note: We must not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
        bool isObjectFoundInCache = gHashStrLookup.find(objectFilenameHash, palanteerStrHash);
        gLkupMutex.unlock();

        if(isObjectFoundInCache) {
            filenameHash = palanteerStrHash; // And filename is still zero (already known by Palanteer, so similar to a static string)
        } else {
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
            gLkupMutex.lock();
            gHashStrLookup.insert(objectFilenameHash, palanteerStrHash); // We let the Palanteer filenameHash set to zero because it is a dynamic string
            gLkupMutex.unlock();
            Py_INCREF(arg);
        }
        if(palanteerStrHash==gThisModuleNameHash) return; // Filter this module

        // Function/name
        gLkupMutex.lock(); // Note: We must not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
        isObjectFoundInCache = gHashStrLookup.find(objectNameHash, palanteerStrHash);
        gLkupMutex.unlock();

        if(isObjectFoundInCache) {
            nameHash = palanteerStrHash; // And name is still zero (already known by Palanteer)
        }
        else {
            name = cfn->m_ml->ml_name; // And nameHash is still zero (dynamic string for Palanteer)
            palanteerStrHash = plPriv::hashString(name);
            gLkupMutex.lock();
            gHashStrLookup.insert(objectNameHash, palanteerStrHash); // We let the Palanteer nameHash set to zero because it is a dynamic string
            gLkupMutex.unlock();
            Py_INCREF(arg);
        }
    } // End of case of C function

    else { // Python function (or method)
        PyCodeObject*     objectCode         = frame->f_code;
        plPriv::hashStr_t objectNameHash     = pyHashString(objectCode);
        plPriv::hashStr_t objectFilenameHash = pyHashString(objectCode->co_filename);
        lineNbr                              = objectCode->co_firstlineno;

        gLkupMutex.lock(); // Note: We must not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
        bool isObjectFoundInCache = gHashStrLookup.find(objectNameHash, palanteerStrHash);
        gLkupMutex.unlock();

        // Function/name
        if(isObjectFoundInCache) {
            nameHash = palanteerStrHash; // And name is still zero (already known by Palanteer)
            for(int i=0; i<gFunctionsToHideQty; ++i) if(nameHash==gFunctionsToHide[i]) return; // Some filtering
        }
        else {
            PyFrame_FastToLocals(frame); // Update locals for access
            // Try getting the class name only if the first argument is "self"
            if(objectCode->co_argcount && !strcmp(PyUnicode_AsUTF8(PyTuple_GET_ITEM(objectCode->co_varnames, 0)), "self")) {
                PyObject* locals = frame->f_locals;
                if(locals) {
                    PyObject* self = PyDict_GetItemString(locals, "self");
                    if(self) {
                        PyObject* class_obj = PyObject_GetAttrString(self, "__class__");
                        if(class_obj) {
                            PyObject* class_name = PyObject_GetAttrString(class_obj, "__name__");
                            if(class_name) {  // The name is "<class>.<symbol>"
                                snprintf(tmpStr, sizeof(tmpStr), "%s.%s", PyUnicode_AsUTF8(class_name), PyUnicode_AsUTF8(objectCode->co_name));
                                name = tmpStr;
                                Py_DECREF(class_name);
                            }
                            Py_DECREF(class_obj);
                        }
                    }
                }
            }

            // If the class name was not found, then we just use "<symbol>"
            if(!name) {
                name = PyUnicode_AsUTF8(objectCode->co_name); // And nameHash is still zero (dynamic string for Palanteer)
            }

            // Update the lookup
            palanteerStrHash = plPriv::hashString(name);
            gLkupMutex.lock();
            gHashStrLookup.insert(objectNameHash, palanteerStrHash); // We let the Palanteer nameHash set to zero because it is a dynamic string
            gLkupMutex.unlock();
            Py_INCREF(objectCode);
            for(int i=0; i<gFunctionsToHideQty; ++i) if(palanteerStrHash==gFunctionsToHide[i]) return; // Some filtering (after hash update)
        }


        // Module/filename
        gLkupMutex.lock(); // Note: We must not call Python function with a lock taken (with the GIL, it would create a double mutex deadlock)
        isObjectFoundInCache = gHashStrLookup.find(objectFilenameHash, palanteerStrHash);
        gLkupMutex.unlock();

        if(isObjectFoundInCache) {
            filenameHash = palanteerStrHash; // And filename is still zero (already known by Palanteer, so similar to a static string)
        } else {
            filename         = PyUnicode_AsUTF8(objectCode->co_filename);
            palanteerStrHash = plPriv::hashString(filename);
            gLkupMutex.lock();
            gHashStrLookup.insert(objectFilenameHash, palanteerStrHash); // We let the Palanteer filenameHash set to zero because it is a dynamic string
            gLkupMutex.unlock();
            Py_INCREF(objectCode);
            Py_INCREF(objectCode->co_filename);
        }

        // Update of the stack per thread (used by the manual calls to get location info)
        if(isEnter) {
            plAssert(gThreads.stackDepth<STACK_MAX_DEPTH);
            gThreads.stack[gThreads.stackDepth].filenameHash = palanteerStrHash; // Save scope information
            gThreads.stack[gThreads.stackDepth].lineNbr      = lineNbr;
            if(gThreads.stackDepth<STACK_MAX_DEPTH-1) gThreads.stackDepth++;
        }
        else if(gThreads.stackDepth>0) gThreads.stackDepth--;

    } // End of case of Python function

    // Log
    const char* sentFilename = filename? plPriv::getDynString(filename) : 0;
    const char* sentName     = name?     plPriv::getDynString(name) : 0;
    uint32_t bi = plPriv::globalCtx.bankAndIndex.fetch_add(1);
    plPriv::eventLogBase(bi, filenameHash, nameHash, sentFilename, sentName, lineNbr,
                         PL_FLAG_TYPE_DATA_TIMESTAMP | (isEnter? PL_FLAG_SCOPE_BEGIN : PL_FLAG_SCOPE_END))
        .vU64 = PL_GET_CLOCK_TICK_FUNC();
    plPriv::eventCheckOverflow(bi);
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

    // Save the error state (this function shall be "transparent")
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);

    // Only the bottom of the exception stack is processed
    if(gThreads.nextExceptionFrame!=frame) {
        // Log a marker with the line number (function is the current scope) and the exception text
        PyObject* exceptionRepr = PyObject_Repr(PyTuple_GET_ITEM(arg, 1)); // Representation of the exception "value"
        char msg[256];
        snprintf(msg, sizeof(msg), "line %d: %s", PyFrame_GetLineNumber(frame), PyUnicode_AsUTF8(exceptionRepr));
        plPriv::hashStr_t palanteerCategoryStrHash, palanteerMsgStrHash, strHash;

        gLkupMutex.lock();
        CACHE_STRING("Exception", palanteerCategoryStrHash);
        CACHE_STRING(msg,         palanteerMsgStrHash);
        gLkupMutex.unlock();
        pyEventLogRaw(palanteerMsgStrHash, palanteerCategoryStrHash, msg, "Exception", 0, PL_FLAG_TYPE_MARKER, PL_GET_CLOCK_TICK_FUNC());
        Py_XDECREF(exceptionRepr);
    }

    // Store upper level so that we skip it
    gThreads.nextExceptionFrame = frame->f_back;
    gThreads.isBootstrap        = 0;

    // Restore the error state
    if(ptype) PyErr_Restore(ptype, pvalue, ptraceback);
    return 0;
}


// Manual instrumentation
// ======================

#define DATA_LOGGING_HEADER()                                           \
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;                               \
    if(gThreads.stackDepth==0) {                                        \
        PyErr_SetString(PyExc_TypeError, "Data must be logged inside a scope (root here). Either move in a function or use plBegin/plEnd to create a root scope."); \
        return 0;                                                       \
    }                                                                   \
    ThreadStackElem_t& tc = gThreads.stack[gThreads.stackDepth-1]


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
    gLkupMutex.lock();
    CACHE_STRING(name, palanteerStrHash);
    gLkupMutex.unlock();

    // Log
    pyEventLogRaw(plPriv::hashString(""), palanteerStrHash, 0, name, 0, PL_FLAG_TYPE_THREADNAME, 0);

    Py_RETURN_NONE;
}


static PyObject*
pyPlData(PyObject* Py_UNUSED(self), PyObject* args)
{
    DATA_LOGGING_HEADER();

    // Parse arguments
    char* name = 0;
    PyObject* dataObj = 0;
    if(!PyArg_ParseTuple(args, "sO", &name, &dataObj)) { Py_RETURN_NONE; }

    // Get the data name
    plPriv::hashStr_t palanteerStrHash, strHash;
    gLkupMutex.lock();
    CACHE_STRING(name, palanteerStrHash);
    gLkupMutex.unlock();

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
        gLkupMutex.lock();
        CACHE_STRING(valueStr, palanteerValueStrHash);
        gLkupMutex.unlock();
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

    // Parse arguments
    char* category = 0, *msg = 0;
    if(!PyArg_ParseTuple(args, "ss", &category, &msg)) { Py_RETURN_NONE; }

    plPriv::hashStr_t palanteerCategoryStrHash, palanteerMsgStrHash, strHash;
    gLkupMutex.lock();
    CACHE_STRING(category, palanteerCategoryStrHash);
    CACHE_STRING(msg,      palanteerMsgStrHash);
    gLkupMutex.unlock();

    pyEventLogRaw(palanteerMsgStrHash, palanteerCategoryStrHash, msg, category, 0, PL_FLAG_TYPE_MARKER, PL_GET_CLOCK_TICK_FUNC());

    Py_RETURN_NONE;
}


static PyObject*
pyPlLockWait(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;

    // Parse arguments
    char* name = 0;
    if(!PyArg_ParseTuple(args, "s", &name)) { Py_RETURN_NONE; }

    if(gWithFunctions && gThreads.stackDepth!=0) {
        plPriv::hashStr_t palanteerStrHash, strHash;
        gLkupMutex.lock();
        CACHE_STRING(name, palanteerStrHash);
        gLkupMutex.unlock();
        ThreadStackElem_t& tc = gThreads.stack[gThreads.stackDepth-1];
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

    // Parse arguments
    char* name  = 0;
    int   state = 0;
    if(!PyArg_ParseTuple(args, "sp", &name, &state)) { Py_RETURN_NONE; }

    if(gWithFunctions && gThreads.stackDepth!=0) {
        plPriv::hashStr_t palanteerStrHash, strHash;
        gLkupMutex.lock();
        CACHE_STRING(name, palanteerStrHash);
        gLkupMutex.unlock();
        ThreadStackElem_t& tc = gThreads.stack[gThreads.stackDepth-1];
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

    // Parse arguments
    char* name = 0;
    if(!PyArg_ParseTuple(args, "s", &name)) { Py_RETURN_NONE; }

    if(gWithFunctions && gThreads.stackDepth!=0) {
        plPriv::hashStr_t palanteerStrHash, strHash;
        gLkupMutex.lock();
        CACHE_STRING(name, palanteerStrHash);
        gLkupMutex.unlock();
        ThreadStackElem_t& tc = gThreads.stack[gThreads.stackDepth-1];
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
    plAssert(gThreads.stackDepth<STACK_MAX_DEPTH);
    gThreads.stack[gThreads.stackDepth].filenameHash = palanteerFilenameStrHash? palanteerFilenameStrHash : plPriv::hashString(filename); // Save scope information
    gThreads.stack[gThreads.stackDepth].lineNbr = lineNbr; // Save scope information
    if(gThreads.stackDepth<STACK_MAX_DEPTH-1) gThreads.stackDepth++;
    Py_RETURN_NONE;
}


static PyObject*
pyPlEnd(PyObject* Py_UNUSED(self), PyObject* args)
{
    if(!PL_IS_ENABLED_()) Py_RETURN_NONE;
    if(gThreads.stackDepth<=0) {
        PyErr_SetString(PyExc_TypeError, "plEnd is called at the scope root. Check that all plBegin get a corresponding plEnd.");
        return 0;
    }
    ThreadStackElem_t& tc = gThreads.stack[gThreads.stackDepth-1];

    // Parse arguments
    char* name = (char*)"";
    if(!PyArg_ParseTuple(args, "|s", &name)) {
        PyErr_SetString(PyExc_TypeError, "Unable to decode the parameter. A string is expected.");
        return 0;
    }

    plPriv::hashStr_t palanteerStrHash, strHash;
    gLkupMutex.lock();
    CACHE_STRING(name, palanteerStrHash);
    gLkupMutex.unlock();

    // Log
    pyEventLogRaw(tc.filenameHash, palanteerStrHash, 0, name, tc.lineNbr, PL_FLAG_SCOPE_END | PL_FLAG_TYPE_DATA_TIMESTAMP, PL_GET_CLOCK_TICK_FUNC());

    // Update the stack
    gThreads.stackDepth--;

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
    bool status = gCliHandlerLookup.find(cio.getCliNameHash(), cliHandlerObj);
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
    PyObject* answerObj = PyEval_CallObject(cliHandlerObj, argTuple);

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
    gCliHandlerLookup.insert(plPriv::hashString(name), cliHandlerObj);
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
