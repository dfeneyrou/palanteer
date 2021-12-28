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


// This file is the implementation of the instrumentation library for Python language,
//  as a C-extension. It mainly wraps the C++ instrumentation library.


// System includes
#include <mutex>

// CPython includes
#include "Python.h"
#include "frameobject.h"

// Local includess
#include "palanteer.h"
#include "pyInterface.h"
#include "pyMainItf.h"


// State
// =====

// Main Palanteer entry
static pyMainItf* pyPlInstance = 0;

// Python callbacks
static PyObject* pyNotifyRecordStarted = 0;
static PyObject* pyNotifyRecordEnded = 0;
static PyObject* pyNotifyLog = 0;
static PyObject* pyNotifyCommandAnswer = 0;
static PyObject* pyNotifyNewFrozenThreadState = 0;
static PyObject* pyNotifyNewStrings = 0;
static PyObject* pyNotifyNewCollectionTick = 0;
static PyObject* pyNotifyNewThreads = 0;
static PyObject* pyNotifyNewElems = 0;
static PyObject* pyNotifyNewClis = 0;
static PyObject* pyNotifyNewEvents = 0;


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


// Re-routed Palanteer notifications toward Python layer
// =====================================================

extern "C"
void notifyRecordStarted(const char* appName, const char* buildName, bool areStringsExternal, bool isStringHashShort, bool isControlEnabled)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    PyObject* returnedObject = PyObject_CallFunction(pyNotifyRecordStarted, "ssiii", appName, buildName,
                                                     (int)areStringsExternal, (int)isStringHashShort, (int)isControlEnabled);
    plAssert(returnedObject);
    Py_DECREF(returnedObject);
    PyGILState_Release(gstate);
}

extern "C"
void notifyRecordEnded(void)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    PyObject* returnedObject = PyObject_CallFunction(pyNotifyRecordEnded, "");
    plAssert(returnedObject);
    Py_DECREF(returnedObject);
    PyGILState_Release(gstate);
}

extern "C"
void notifyLog(int level, const char* msg)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* returnedObject = PyObject_CallFunction(pyNotifyLog, "is", level, msg);
    plAssert(returnedObject);
    Py_DECREF(returnedObject);
    PyGILState_Release(gstate);
}

extern "C"
void notifyCommandAnswer(int status, const char* answer)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    PyObject* returnedObject = PyObject_CallFunction(pyNotifyCommandAnswer, "is", status, answer);
    plAssert(returnedObject);
    Py_DECREF(returnedObject);
    PyGILState_Release(gstate);
}

extern "C"
void notifyNewFrozenThreadState(u64 frozenThreadBitmap)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    PyObject* returnedObject = PyObject_CallFunction(pyNotifyNewFrozenThreadState, "K", frozenThreadBitmap);
    plAssert(returnedObject);
    Py_DECREF(returnedObject);
    PyGILState_Release(gstate);
}

extern "C"
void notifyNewStrings(pyiString* strings, int stringQty)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    // Build the list of tuples (hash, name)
    PyObject* stringList = PyList_New(0);
    for(int i=0; i<stringQty; ++i) {
        PyObject* aTuple = PyTuple_New(2);
        PyTuple_SET_ITEM(aTuple, 0,  PyLong_FromUnsignedLongLong(strings[i].nameHash));
        PyTuple_SET_ITEM(aTuple, 1,  PyUnicode_FromString(strings[i].name));
        PyList_Append(stringList, aTuple);
    }
    // Call the python
    PyObject* result = PyObject_CallFunction(pyNotifyNewStrings, "N", stringList);
    plAssert(result);
    Py_DECREF(stringList);
    Py_DECREF(result);
    PyGILState_Release(gstate);
}

extern "C"
void notifyNewCollectionTick(void)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    // Call the python
    PyObject* result = PyObject_CallFunction(pyNotifyNewCollectionTick, "");
    plAssert(result);
    Py_DECREF(result);
    PyGILState_Release(gstate);
}

extern "C"
void notifyNewThreads(pyiThread* threads, int threadQty)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    // Build the list of tuples (hash, threadId)
    PyObject* threadList = PyList_New(0);
    for(int i=0; i<threadQty; ++i) {
        PyObject* aTuple = PyTuple_New(2);
        PyTuple_SET_ITEM(aTuple, 0,  PyLong_FromUnsignedLongLong(threads[i].nameHash));
        PyTuple_SET_ITEM(aTuple, 1,  PyLong_FromLong(threads[i].threadId));
        PyList_Append(threadList, aTuple);
    }
    // Call the python
    PyObject* result = PyObject_CallFunction(pyNotifyNewThreads, "N", threadList);
    plAssert(result);
    Py_DECREF(threadList);
    Py_DECREF(result);
    PyGILState_Release(gstate);
}

extern "C"
void notifyNewElems(pyiElem* elems, int elemQty)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    // Build the list of tuples
    PyObject* elemList = PyList_New(0);
    for(int i=0; i<elemQty; ++i) {
        PyObject* aTuple = PyTuple_New(5);
        PyTuple_SET_ITEM(aTuple, 0,  PyLong_FromUnsignedLongLong(elems[i].nameHash));
        PyTuple_SET_ITEM(aTuple, 1,  PyLong_FromLong(elems[i].elemIdx));
        PyTuple_SET_ITEM(aTuple, 2,  PyLong_FromLong(elems[i].prevElemIdx));
        PyTuple_SET_ITEM(aTuple, 3,  PyLong_FromLong(elems[i].threadId));
        PyTuple_SET_ITEM(aTuple, 4,  PyLong_FromLong(elems[i].flags));
        PyList_Append(elemList, aTuple);
    }
    // Call the python
    PyObject* result = PyObject_CallFunction(pyNotifyNewElems, "N", elemList);
    plAssert(result);
    Py_DECREF(elemList);
    Py_DECREF(result);
    PyGILState_Release(gstate);
}

extern "C"
void notifyNewClis(pyiCli* clis, int cliQty)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    // Build the list of tuples (name, paramSpec, description)
    PyObject* cliList = PyList_New(0);
    for(int i=0; i<cliQty; ++i) {
        PyObject* aTuple = PyTuple_New(3);
        PyTuple_SET_ITEM(aTuple, 0,  PyUnicode_FromString(clis[i].name));
        PyTuple_SET_ITEM(aTuple, 1,  PyUnicode_FromString(clis[i].paramSpec));
        PyTuple_SET_ITEM(aTuple, 2,  PyUnicode_FromString(clis[i].description));
        PyList_Append(cliList, aTuple);
    }
    // Call the python
    PyObject* result = PyObject_CallFunction(pyNotifyNewClis, "N", cliList);
    plAssert(result);
    Py_DECREF(cliList);
    Py_DECREF(result);
    PyGILState_Release(gstate);
}

extern "C"
void notifyNewEvents (pyiEvent* events, int eventQty)
{
    PyGILState_STATE gstate  = PyGILState_Ensure();
    // Build the list of tuples
    PyObject* eventList = PyList_New(0);
    for(int i=0; i<eventQty; ++i) {
        PyObject* aTuple = PyTuple_New(6);
        PyTuple_SET_ITEM(aTuple, 0,  PyLong_FromLong(events[i].specId));
        PyTuple_SET_ITEM(aTuple, 1,  PyLong_FromLong(events[i].elemId));
        PyTuple_SET_ITEM(aTuple, 2,  PyLong_FromLong(events[i].childrenQty));
        PyTuple_SET_ITEM(aTuple, 3,  PyLong_FromUnsignedLongLong(events[i].nameHash));
        PyTuple_SET_ITEM(aTuple, 4,  PyLong_FromLongLong(events[i].dateNs));
        PyTuple_SET_ITEM(aTuple, 5,  PyLong_FromUnsignedLongLong(events[i].value));
        PyList_Append(eventList, aTuple);
    }
    // Call the python
    PyObject* result = PyObject_CallFunction(pyNotifyNewEvents, "N", eventList);
    plAssert(result);
    Py_DECREF(eventList);
    Py_DECREF(result);
    PyGILState_Release(gstate);
}


// Commands called from the Python layer
// =====================================

static PyObject*
serverStart(PyObject* Py_UNUSED(self), PyObject* args)
{
    int rxPort;
    if(PyArg_ParseTuple(args, "i", &rxPort)) {
        if(!pyPlInstance) {

            // Python notification storage
            PyObject* myModuleString = PyUnicode_FromString("palanteer_scripting._scripting");
            plAssert(myModuleString);
            PyObject* myModule = PyImport_Import(myModuleString);

#define GET_NOTIF(cName, pyName)                                \
            cName = PyObject_GetAttrString(myModule, #pyName);  \
            plAssert(cName);                                    \
            Py_INCREF(cName);

            GET_NOTIF(pyNotifyRecordStarted, _notify_record_started);
            GET_NOTIF(pyNotifyRecordEnded,   _notify_record_ended);
            GET_NOTIF(pyNotifyLog,           _notify_log);
            GET_NOTIF(pyNotifyCommandAnswer, _notify_command_answer);
            GET_NOTIF(pyNotifyNewFrozenThreadState, _notify_new_frozen_thread_state);
            GET_NOTIF(pyNotifyNewStrings,    _notify_new_strings);
            GET_NOTIF(pyNotifyNewCollectionTick, _notify_new_collection_tick);
            GET_NOTIF(pyNotifyNewThreads,    _notify_new_threads);
            GET_NOTIF(pyNotifyNewElems,      _notify_new_elems);
            GET_NOTIF(pyNotifyNewClis,       _notify_new_clis);
            GET_NOTIF(pyNotifyNewEvents,     _notify_new_events);

            pyiNotifications ntf = {
                notifyRecordStarted,
                notifyRecordEnded,
                notifyLog,
                notifyCommandAnswer,
                notifyNewFrozenThreadState,
                notifyNewStrings,
                notifyNewCollectionTick,
                notifyNewThreads,
                notifyNewElems,
                notifyNewClis,
                notifyNewEvents
            };

            Py_BEGIN_ALLOW_THREADS
            pyPlInstance = new pyMainItf(rxPort, ntf);
            Py_END_ALLOW_THREADS
        }
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Integer parameter expected");
        return 0;
    }

    // On Python equal or less than 3.6, multithreading&GIL is not activated by default. No-op on 3.7+ (and deprecated on 3.9)
#if PY_MINOR_VERSION < 9
    PyEval_InitThreads();
#endif

    Py_RETURN_NONE;
}


static PyObject*
serverStop(PyObject* Py_UNUSED(self), PyObject* args)
{
    delete pyPlInstance;
    pyPlInstance = 0;
    Py_RETURN_NONE;
}


static PyObject*
setRecordFilename(PyObject* Py_UNUSED(self), PyObject* args)
{
    const char* recordFilename;
    if(PyArg_ParseTuple(args, "z", &recordFilename)) {
        pyPlInstance->setRecordFilename(recordFilename);
    }
    else PyErr_SetString(PyExc_TypeError, "String parameter or None expected");

    Py_RETURN_NONE;
}


static PyObject*
setMaxLatencyMs(PyObject* Py_UNUSED(self), PyObject* args)
{
    int maxLatencyMs;
    if(PyArg_ParseTuple(args, "i", &maxLatencyMs)) {
        pyPlInstance->setMaxLatencyMs(maxLatencyMs);
    }
    else PyErr_SetString(PyExc_TypeError, "Integer parameter expected");

    Py_RETURN_NONE;
}


static PyObject*
setFreezeModeState(PyObject* Py_UNUSED(self), PyObject* args)
{
    int state;
    if(PyArg_ParseTuple(args, "i", &state)) {
        pyPlInstance->setFreezeMode(state);
    }
    else PyErr_SetString(PyExc_TypeError, "Bool parameter expected");

    Py_RETURN_NONE;
}


static PyObject*
sendCliRequest(PyObject* Py_UNUSED(self), PyObject* args)
{
    const char* commandStr;
    if(PyArg_ParseTuple(args, "s", &commandStr)) {
        bsVec<bsString> commands;
        commands.push_back(commandStr);
        pyPlInstance->cli(commands);
    }
    else PyErr_SetString(PyExc_TypeError, "String parameter expected");

    Py_RETURN_NONE;

}


static PyObject*
stepContinue(PyObject* Py_UNUSED(self), PyObject* args)
{
    u64 threadBitmap;
    if(PyArg_ParseTuple(args, "K", &threadBitmap)) {
        pyPlInstance->stepContinue(threadBitmap);
    }
    else PyErr_SetString(PyExc_TypeError, "64 bit unsigned integer parameter expected");

    Py_RETURN_NONE;
}


static PyObject*
killProgram(PyObject* Py_UNUSED(self), PyObject* args)
{
    pyPlInstance->killProgram();
    Py_RETURN_NONE;
}


static PyObject*
clearBufferedEvents(PyObject* Py_UNUSED(self), PyObject* args)

{
    // Some locks are taken inside the C code
    Py_BEGIN_ALLOW_THREADS
    pyPlInstance->clearBufferedEvents();
    Py_END_ALLOW_THREADS
    Py_RETURN_NONE;
}


static PyObject*
clearAllSpecs(PyObject* Py_UNUSED(self), PyObject* args)
{
    // Some locks are taken inside the C code
    Py_BEGIN_ALLOW_THREADS
    pyPlInstance->clearAllSpecs();
    Py_END_ALLOW_THREADS
    Py_RETURN_NONE;
}


static PyObject*
addSpec(PyObject* Py_UNUSED(self), PyObject* args)
{
    // Parse the parameters
    const char* threadName = 0;
    u64         threadHash = 0LL;
    PyObject*   objParentPath, *objElemArray;
    if(!PyArg_ParseTuple(args, "sKOO", &threadName, &threadHash, &objParentPath, &objElemArray)) {
        PyErr_SetString(PyExc_TypeError, "Unable to decode the parameters.");
        return 0;
    }

    // Parse the parent path (tuple of strings)
    if(!PyTuple_Check(objParentPath)) {
        PyErr_SetString(PyExc_TypeError, "Parent path shall be a tuple.");
        return 0;
    }
    pyiSpec parentPath = {0, 0};
    parentPath.pathQty = (int)PyTuple_Size(objParentPath);
    parentPath.path    = (pyiPath*) alloca(parentPath.pathQty*sizeof(pyiPath));
    for(int i=0; i<parentPath.pathQty; ++i) {
        PyObject* pathChunk = PyTuple_GET_ITEM(objParentPath, i);

        if(!PyTuple_Check(pathChunk) || PyTuple_Size(pathChunk)!=2) {
            PyErr_SetString(PyExc_TypeError, "Parent path's chunk shall be a tuple of size 2.");
            return 0;
        }

        const char* name = 0;
        u64         hash = 0LL;
        if(!PyArg_ParseTuple(pathChunk, "sK", &name, &hash)) {
            PyErr_SetString(PyExc_TypeError, "Unable to decode the parent path chunk tuple.");
            return 0;
        }

        parentPath.path[i].name = name;
        parentPath.path[i].hash = hash;
    }

    // Parse the list of elements (list of tuples of strings)
    if(!PyList_Check(objElemArray)) {
        PyErr_SetString(PyExc_TypeError, "The elem array shall be a list.");
        return 0;
    }
    int      elemQty   = (int)PyList_Size(objElemArray);
    pyiSpec* elemArray = (pyiSpec*) alloca(elemQty*sizeof(pyiSpec));
    for(int j=0; j<elemQty; ++j) {
        PyObject* objElem = PyList_GET_ITEM(objElemArray, j);
        if(!PyTuple_Check(objElem)) {
            PyErr_SetString(PyExc_TypeError, "Elem array shall be a list of specs.");
            return 0;
        }

        elemArray[j].pathQty = (int)PyTuple_Size(objElem);
        elemArray[j].path = (pyiPath*) alloca(elemArray[j].pathQty*sizeof(pyiPath));
        for(int i=0; i<elemArray[j].pathQty; ++i) {
            PyObject* pathChunk = PyTuple_GET_ITEM(objElem, i);

            if(!PyTuple_Check(pathChunk) || PyTuple_Size(pathChunk)!=2) {
                PyErr_SetString(PyExc_TypeError, "Element's path chunk shall be a tuple of size 2.");
                return 0;
            }

            const char* name = 0;
            u64         hash = 0LL;
            if(!PyArg_ParseTuple(pathChunk, "sK", &name, &hash)) {
                PyErr_SetString(PyExc_TypeError, "Unable to decode the element path chunk tuple.");
                return 0;
            }

            elemArray[j].path[i].name = name;
            elemArray[j].path[i].hash = hash;
        } // End of loop on the path chunks of a spec
    } // End of loop on the specs

    // Some locks are taken inside the C code
    Py_BEGIN_ALLOW_THREADS
    pyPlInstance->addSpec(threadName, threadHash, &parentPath, elemArray, elemQty);
    Py_END_ALLOW_THREADS

    Py_RETURN_NONE;
}


static PyObject*
getUnresolvedElemInfos(PyObject* Py_UNUSED(self), PyObject* args)
{
    pyiDebugSpecInfo* infoArray = 0;
    int infoQty = 0;

    // Some locks are taken inside the C code
    Py_BEGIN_ALLOW_THREADS
    pyPlInstance->getUnresolvedElemInfos(&infoArray, &infoQty);
    Py_END_ALLOW_THREADS

    PyObject* ueiList = PyList_New(0);
    for(int i=0; i<infoQty; ++i) {
        const pyiDebugSpecInfo& ia = infoArray[i];
        PyObject* aTuple = PyTuple_New(3);
        PyTuple_SET_ITEM(aTuple, 0,  PyLong_FromLong(ia.specId));
        PyTuple_SET_ITEM(aTuple, 1,  PyLong_FromLong(ia.elemId));
        PyTuple_SET_ITEM(aTuple, 2,  PyUnicode_FromString(ia.errorMsg));
        PyList_Append(ueiList, aTuple);
    }

    return ueiList;
}


// Python module glue
// ==================

static PyMethodDef module_methods[] = {
    {"server_start",          serverStart,         METH_VARARGS, 0},
    {"server_stop",           serverStop,          METH_VARARGS, 0},
    {"set_record_filename",   setRecordFilename,   METH_VARARGS, 0},
    {"set_max_latency_ms",    setMaxLatencyMs,     METH_VARARGS, 0},
    {"set_freeze_mode",       setFreezeModeState,  METH_VARARGS, 0},
    {"send_cli_request",      sendCliRequest,      METH_VARARGS, 0},
    {"step_continue",         stepContinue,        METH_VARARGS, 0},
    {"kill_program",          killProgram,         METH_VARARGS, 0},
    {"clear_buffered_events", clearBufferedEvents, METH_VARARGS, 0},
    {"clear_all_specs",       clearAllSpecs,       METH_VARARGS, 0},
    {"add_spec",              addSpec,             METH_VARARGS, 0},
    {"get_unresolved_elem_infos", getUnresolvedElemInfos, METH_VARARGS, 0},

    {0, 0, 0, 0} // End of list
};


PyMODINIT_FUNC
PyInit__cextension(void)
{
    static struct PyModuleDef totoModuleDef = { PyModuleDef_HEAD_INIT, "_cextension", "Palanteer scripting module C extension",
                                                -1, module_methods, 0, 0, 0, 0 };
    PyObject* m = PyModule_Create(&totoModuleDef);
    return m;
}
