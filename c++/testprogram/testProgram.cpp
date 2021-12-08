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

// This file is a test program with multiple purposes:
//  - show an example of C++ instrumentation
//  - have a way to measure speed performance in a specific case
//  - be a part of Palanteer internal tests, by using all instrumentation APIs and features


#define _WINSOCKAPI_
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include <cstdint>
#include <list>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include "windows.h"
#endif  // _WIN32

#ifndef PL_GROUP_PL_COLLECT
#define PL_GROUP_PL_COLLECT 1
#endif

#define PL_IMPLEMENTATION 1
#define PL_IMPL_COLLECTION_BUFFER_BYTE_QTY 60000000  // Dimensioned for the demanding "performance" evaluation
#include "palanteer.h"

#include "testPart.h"

// Instrumentation group to test the group API
#ifndef PL_GROUP_TESTGROUP
#define PL_GROUP_TESTGROUP 1
#endif


// ==============================
// Globals & definitions
// ==============================

#if defined(_WIN32) && !defined(strcasecmp)
#define strcasecmp _stricmp
#endif

#define GET_TIME(unit) std::chrono::duration_cast<std::chrono::unit>(std::chrono::steady_clock::now().time_since_epoch()).count()

std::vector<Synchro> groupSynchro;
std::mutex           globalSharedMx;
RandomLCM            globalRandomGenerator;


// ==============================
// Functions of the "control" task
// ==============================

float
otherSubTask(int taskNbr, int iterNbr)
{
    plScope("otherSubTask");
    plVar(taskNbr, iterNbr);

    // Allocate something
    int* dummyAlloc = new int[globalRandomGenerator.get(1000, 5000)];

    // Compute something
    float dummyValue = busyWait(globalRandomGenerator.get(500, 1000));

    delete[] dummyAlloc;

    {
        plScope("doSomethingUseful");
        dummyValue += busyWait(globalRandomGenerator.get(100, 500));

        for(int i=0; i<(7*taskNbr*iterNbr)%3; ++i) {
            plScope("Partial work");
            dummyValue += busyWait(globalRandomGenerator.get(100, 500));
        }
    }
    // Log something visual
    double x = 0.2*(0.25*taskNbr+iterNbr)+0.1; (void)x;
    plVar(exp(x)/x);

    return dummyValue;
}


float
subTaskUsingSharedResource(int taskNbr, int iterNbr)
{
    plFunctionDyn();
    static const char* fruits[5] = { "apple", "orange", "durian", "banana", "grenada" };
    plString_t     vegetables[4] = { plMakeString("carrot"), plMakeString("onion"), plMakeString("bean"), plMakeString("patato") }; // "External strings" feature hides this
    (void)vegetables; (void)iterNbr; // Remove warnings when Palanteer events are not used

    plData("input value##hexa", taskNbr);  // This "hexa" unit is special, this integer value will be displayed in hexadecimal on viewer.

    // Compute something
    float dummyValue = busyWait(150);

    // Allocate something
    int* dummyAlloc = new int[globalRandomGenerator.get(100, 500)];

    std::list<std::string> superList;
    for(int i=0; i<5000; ++i) {
        plScope("Add fruit");
        superList.push_back(fruits[(taskNbr+i*7)%5]);
    }
    plVar(superList.back().c_str());
    plData("Ingredient for the soup##ingredient", vegetables[(taskNbr+iterNbr*7)%4]); // The unit is declared as "ingredient"

    // Log something visual
    plData("Computation output##parsec", cos(1.5*(0.25*taskNbr+iterNbr))); // The unit is declared as "parsec"

    delete[] dummyAlloc;

    return dummyValue;
}


// Thread entry point
void
controlTask(int groupNbr, const char* groupName, int durationMultiplier)
{
    plString_t compileTimeStrings[2] = { plMakeString("Even"), plMakeString("Odd") };
    (void)compileTimeStrings; // Remove warnings when Palanteer events are not used

    plDeclareThreadDyn("%s%sControl", groupName, strlen(groupName)? "/":"");

    int   iterationQty = 10*durationMultiplier;
    float dummyValue = 0;
    Synchro& synchro = groupSynchro[groupNbr];
    std::list<int*> allocationList;
    std::string synchroLockName = strlen(groupName)? (std::string(groupName) + " synchro") : "synchro";

    plFreezePoint();

    for(int iterNbr=0; iterNbr<iterationQty; ++iterNbr) {

        if(globalRandomGenerator.get(0, 100)>=45) {
            // Allocate
            allocationList.push_back(new int[globalRandomGenerator.get(2000, 10000)]);
        } else {
            // Deallocate
            if(!allocationList.empty()) {
                delete[] allocationList.front();
                allocationList.pop_front();
            }
        }

        // Wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(globalRandomGenerator.get(20, 60)));

        // Prepare the Work
        plScope("Iteration");
        plVar(iterNbr, iterationQty);

        // Dynamic but still external string compatible markers
        plMarkerDyn("Count", compileTimeStrings[iterNbr%2]);

        int taskQty = globalRandomGenerator.get(1, 4);
        dummyValue += busyWait(globalRandomGenerator.get(500, 2500));

        for(int taskNbr=0; taskNbr<taskQty; ++taskNbr) {
            plScope("Task");
            plData("Task number", taskNbr);

            dummyValue += busyWait(globalRandomGenerator.get(300, 1000));

            {
                plLockWait("Shared resource");
                std::lock_guard<std::mutex> lk(globalSharedMx);
                plLockScopeState("Shared resource", true);
                dummyValue += subTaskUsingSharedResource(taskNbr, iterNbr);
            } // Unlock automatically logged because of "plLockScopeState"
            dummyValue += busyWait(globalRandomGenerator.get(10, 200));

            dummyValue += otherSubTask(taskNbr, iterNbr);
        }

        // Send a signal to the associated task
        std::unique_lock<std::mutex> lk(synchro.mx);
        synchro.command.store((iterNbr==iterationQty-1)? 2 : 1); // 2 means the termination of the associated thread
        plLockNotifyDyn(synchroLockName.c_str());
        synchro.cv.notify_one();
    }

    plBegin("Final result");
    plVar(dummyValue);
    plEnd("Final result");

    // Clean allocations
    while(!allocationList.empty()) {
        delete[] allocationList.front();
        allocationList.pop_front();
    }
}


// ==============================
// CLI handlers
// ==============================

void
asyncAssertThread(int condValue)
{
    plDeclareThread("Crash thread");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    plAssert(condValue);
}


void
cliHandlerAsyncAssert(plCliIo& cio)
{
    int condValue = (int)cio.getParamInt(0);
    std::thread t(asyncAssertThread, condValue);
    t.detach();
}


void
cliHandlerCreateMarker(plCliIo& cio)
{
    const char* msg = cio.getParamString(0);
    if(msg) { // In case Palanteer events are not used
        plMarkerDyn("test_marker", msg);
    }
}


void
cliHandlerPoetryGetter(plCliIo& cio)
{
    cio.addToResponse("To bug, or not to bug,");
    cio.addToResponse("that is the question");
}


void
cliHandlerWithParameters(plCliIo& cio)
{
    // Get the params
    int    param1      = (int)cio.getParamInt(0);
    double param2      = cio.getParamFloat(1);
    const char* param3 = cio.getParamString(2);

    // "Complex" handling in order to stimulate important parts of the API
    if(param1<=-1000) {
        cio.addToResponse("This text will not be erased\n");
        cio.setErrorState();
        cio.addToResponse("Error: Very negative first parameter. Not great.");
        return;
    }
    else if(param1<=-100) {
        cio.addToResponse("This text will be erased\n"); // Because "setErrorState" is called with some text
        cio.setErrorState("Error: Mildly negative first parameter. Not great.");
        return;
    }
    else if(param1<=0) {
        cio.setErrorState("Error: First parameter shall be strictly positive (%d seen)", param1);
        return;
    }

    // Build the response
    cio.addToResponse("Strictly positive integer value is: %d\n", param1);
    cio.addToResponse("Float value is: %f\n", param2);
    cio.addToResponse("String value is: %s\n", param3);
}


void
cliHandlerQuit(plCliIo& cio)
{
    (void)cio;
    exit(0);
}


// ==============================
// Performance evaluation program
// ==============================

void
evaluatePerformance(plMode mode, const char* buildName, int durationMultiplier, int serverConnectionTimeoutMsec)
{
    constexpr int iterationQty = 250000; // 4 events per loop
    (void) mode; (void)buildName; (void)serverConnectionTimeoutMsec;

    // Start the logging
    plInitAndStart("C++ perf example", mode, buildName, serverConnectionTimeoutMsec);

    // Give a name to this thread (before or after the library initialization)
    plDeclareThread("Main");

    typedef uint64_t dateNs_t;
    dateNs_t startCollectNs = GET_TIME(nanoseconds);
    int loopQty = iterationQty*durationMultiplier;

    // Logging in loop, 4 events per cycle
    for(int i=0; i<loopQty; ++i) {
        plBegin("TestLoop");
        plData("Iteration", i);
        plData("Still to go", loopQty-i-1);
        plEnd("TestLoop");
    }

    dateNs_t endCollectNs   = GET_TIME(nanoseconds);
    plStopAndUninit();
    dateNs_t endSendingNs   = GET_TIME(nanoseconds);

    // Console display
    plStats  s = plGetStats();
    double bufferUsageRatio    = 100.*(double)s.collectBufferMaxUsageByteQty/(double)((s.collectBufferSizeByteQty>0)? s.collectBufferSizeByteQty:1);
    printf("Collection duration : %.2f ms for %d events\n" \
           "Collection unit cost: %.0f ns\n" \
           "Processing duration : %.2f ms (w/ %s)\n" \
           "Processing rate     : %.3f million event/s\n" \
           "Max buffer usage    : %-7d bytes (%5.2f%% of max)\n",
           (double)(endCollectNs-startCollectNs)/1000000., loopQty*4,
           (double)(endCollectNs-startCollectNs)/(double)(loopQty*4),
           (double)(endSendingNs-startCollectNs)/1000000., (mode==PL_MODE_STORE_IN_FILE)? "disk file writing" : "transmission and server processing",
           4e3*loopQty/(double)(endSendingNs-startCollectNs),
           s.collectBufferMaxUsageByteQty, bufferUsageRatio);
}


// ==============================
// Event collection program
// ==============================

void
collectInterestingData(plMode mode, const char* buildName, int durationMultiplier, int threadGroupQty, int crashKind,
                       int serverConnectionTimeoutMsec)
{
    (void) mode; (void)buildName; (void)serverConnectionTimeoutMsec;

    // Register a CLI before the initialization (this should be the nominal case)
    plRegisterCli(cliHandlerCreateMarker, "test::marker",
                  "msg=string",
                  "Create a marker with the provided string");

    // Give a name to this thread (before or after the library initialization)
    plDeclareThread("Main");

    // Start the logging
    uint64_t startMs = GET_TIME(milliseconds);
    plInitAndStart("C++ example", mode, buildName, serverConnectionTimeoutMsec);

    // CLI registration
    // On purpose *after* the call to plInitAndStart in order to better test the freeze point.
    // This is an exception for this test. It is indeed recommended to register the CLIs *before* the Palanteer initialization
    //   in order to remove any race condition in remote script about calling a not yet registered CLI after connection.
    plRegisterCli(cliHandlerWithParameters, "test::parametersDft",
                  "first=int[[31415926]] second_param=float[[-3.14159265359]] third=string[[no string provided]] fourth=int[[0]]",
                  "Uses the 3 types of parameters with default values and a 4th one");
    plRegisterCli(cliHandlerWithParameters, "test::parameters",
                  "first=int second_param=float third=string",
                  "Uses the 3 types of parameters");
    plRegisterCli(cliHandlerAsyncAssert, "async_assert",
                  "condvalue=int",
                  "Call asynchronously an assertion with the provided value after a 1s timeout");
    plRegisterCli(cliHandlerPoetryGetter,   "get_poetry", "", "Returns some poetry.");
    plRegisterCli(cliHandlerQuit,           "quit",       "", "Exit the program");

    // Freeze points just before starting, and in particular after declaring all CLIs (so that they can be used at this point)
    // These steps are used by Palanter testing
    {
        plFreezePoint();
        plScope("Freeze control test");
        plText("Freeze", "Before first freeze");
        plFreezePoint();
        plText("Freeze", "After first freeze");
        plFreezePoint();
        plText("Freeze", "After second freeze");
    }

    // Launch some active threads
    const char* threadGroupNames[9] = { "", "Workers", "Real time", "Database Cluster", "Helpers", "Engine", "Compute Grid", "Hub", "Idlers" };
    std::vector<std::thread> threads;
    for(int threadGroupNbr=0; threadGroupNbr<threadGroupQty; ++threadGroupNbr) {
        groupSynchro.emplace_back();
    }

    int crashThreadGroupNbr = (crashKind>=0)? time(0)%threadGroupQty : -1; // Random selection of the thread which shall crash
    for(int threadGroupNbr=0; threadGroupNbr<threadGroupQty; ++threadGroupNbr) {
        threads.push_back(std::thread(controlTask,    threadGroupNbr, threadGroupNames[threadGroupNbr], durationMultiplier));
        threads.push_back(std::thread(associatedTask, threadGroupNbr, threadGroupNames[threadGroupNbr],
                                      (crashThreadGroupNbr==threadGroupNbr)? crashKind : -1));
    }

    // Test all the 'group' APIs
    if(plgIsEnabled(TESTGROUP)) { plgFunctionDyn(TESTGROUP); }
    {
        int a = 0; (void)a;
        plgBegin(TESTGROUP, "Group begin/end test");
        plgData(TESTGROUP, "Group variable a", a);
        plgVar(TESTGROUP, a);
        plgMarker(TESTGROUP, "test", "this is a group marker test");
        plgMarkerDyn(TESTGROUP, "test", "this is a group marker test");
        plgMarkerDyn(TESTGROUP, "test", "this is another group marker test");
        plgEnd(TESTGROUP, "Group begin/end test");
    }
    {
        plgScope(TESTGROUP, "Group scope test");
        plgLockWait(TESTGROUP, "Group lock test");
        plgLockState(TESTGROUP, "Group lock test", false);
        plgLockNotify(TESTGROUP, "Group lock test");
    }
    {
        plgScopeDyn(TESTGROUP, "Group scopeDyn test");
        plgLockWaitDyn(TESTGROUP, "Group lock test");
        plgLockStateDyn(TESTGROUP, "Group lock test", false);
        plgLockNotifyDyn(TESTGROUP, "Group lock test");
    }
    { plgLockScopeState(TESTGROUP, "Group lock test", true); }
    { plgLockScopeStateDyn(TESTGROUP, "Group lock test", true); }

#if USE_PL==1 && PL_VIRTUAL_THREADS==1
    // This stimulation is added only if the "virtual threads" feature is activated.
    // A "virtual thread" means a thread that is not controlled by the OS kernel but managed in user space. Typical usages are "fibers"
    // or DES simulations.
    //
    // The goal here is to test the specific APIs that enable the feature, and obviously not to implement such framework.
    // Some OS worker threads are created, they share and run the fake "fibers" (as explained above, no saving of stack context nor registers...)
    // Jobs are represented by a number, they shall be executed one after the other, in loop.
    constexpr int WORKER_THREAD_QTY = 2;
    constexpr int FIBERS_QTY        = 10;  // Caution if you change it: it is also subjected to the thread limitation for tracking, even if not real threads.
    std::atomic<int>   sharedJobIndex(0);
    std::vector<Fiber> fiberPool;
    std::vector<Fiber> fiberWaitingList;
    for(int i=0; i<FIBERS_QTY; ++i) {
        Fiber f;
        f.id = FIBERS_QTY-1-i;  // Small numbers on top, as it is a stack
        fiberPool.push_back(f);
    }
    // Create the  worker threads that will schedule shared jobs in loop. They will stop by themselves.
    for(int workerThreadNbr=0; workerThreadNbr<WORKER_THREAD_QTY; ++workerThreadNbr) {
        threads.push_back(std::thread(fiberWorkerTask, workerThreadNbr, &fiberPool, &fiberWaitingList, &sharedJobIndex));
    }

#endif

    // Wait for threads completion
    plLockWait("Global Synchro");
    for(std::thread& t : threads) t.join();
    plMarker("threading", "All tasks are completed! Joy!");
    plLockState("Global Synchro", false); // End of waiting, no lock used

    // Stop the recording
    plStopAndUninit();

    // Display the statistics
    uint64_t durationMs = GET_TIME(milliseconds)-startMs;
    plStats  s = plGetStats();
    double bufferUsageRatio    = 100.*(double)s.collectBufferMaxUsageByteQty/(double)((s.collectBufferSizeByteQty>0)? s.collectBufferSizeByteQty:1);
    double dynStringUsageRatio = 100.*(double)s.collectDynStringMaxUsageQty/(double)((s.collectDynStringQty>0)? s.collectDynStringQty:1);

    printf("Statistics:\n");
    printf("  Execution time: %d ms\n  Sending calls : %d\n  Sent events   : %d\n  Sent strings  : %d\n",
           (int)durationMs, s.sentBufferQty, s.sentEventQty, s.sentStringQty);
    printf("  Max dyn string usage: %-7d       (%5.2f%% of max)\n", s.collectDynStringMaxUsageQty, dynStringUsageRatio);
    printf("  Max buffer usage    : %-7d bytes (%5.2f%% of max)\n", s.collectBufferMaxUsageByteQty, bufferUsageRatio);
}


// =========================
// Main
// =========================

void
displayUsage(const char* programPath)
{
    printf("\nUsage: %s <parameter> [options]\n", programPath);
    printf("  Palanteer C++ instrumentation test program\n");
    printf("\n");
    printf("  Parameter:\n");
    printf("    'collect'      : Data collection\n");
    printf("    'crash-assert' : Data collection with a planned failed assertion\n");
    printf("    'crash-zerodiv': Data collection with a planned zero division\n");
    printf("    'crash-segv'   : Data collection with a planned seg fault\n");
    printf("    'crash-abort'  : Data collection with a planned abort call\n");
    printf("    'perf'         : Estimation of the logging performances in a loop\n");
    printf("\n");
    printf("  Options to selection the collection mode (exclusive):\n");
    printf("    <Default>: Use remote Palanteer connection\n");
    printf("    '-f'     : Save the record in a file 'example_record.pltraw'\n");
    printf("    '-n'     : No data collection (event recording not enabled at run time)\n");
    printf("\n");
    printf("  Options to configure the program behavior:\n");
    printf("    '-w <millsec>' : Server connection waiting timeout in millisecond (default=-1, no wait)\n");
    printf("    '-t <1-9>      : Defines the quantity of groups of threads (2 threads per group)\n");
    printf("    '-l <integer>' : Run time length multiplier (default is 1)\n");
    printf("    '-b <name>'    : Provide a build name for the current program (default is none)\n");
    printf("    '--port <port>': Use the provided socket port (default is 59059)\n");
    printf("\n");
    printf("To start, you can try this (and look at the testProgram.cpp code too):\n");
    printf("  %s perf    -f   (no need for palanteer, events are stored in the file example_record.pltraw) \n", programPath);
    printf("  %s collect -c   (no need for palanteer, events are displayed on console) \n", programPath);
    printf("  %s collect      (requires the prior launch of 'palanteer' viewer) \n", programPath);
}


int
main(int argc, char** argv)
{
    // Command line parsing and program behavior selection
    enum BehaviorType { NONE, COLLECT, PERF } behavior = NONE;
    bool doDisplayUsage  = false;
    int  crashKind       = -1;  // -1 means no planned crash

    // Get the main type of execution
    if(argc>1) {
        if     (strcasecmp(argv[1], "collect"      )==0)  behavior = COLLECT;
        else if(strcasecmp(argv[1], "perf"         )==0)  behavior = PERF;
        else if(strcasecmp(argv[1], "crash-zerodiv")==0)  crashKind = 0;
        else if(strcasecmp(argv[1], "crash-segv"   )==0)  crashKind = 1;
        else if(strcasecmp(argv[1], "crash-assert" )==0)  crashKind = 2;
        else if(strcasecmp(argv[1], "crash-abort"  )==0)  crashKind = 3;
        else doDisplayUsage = true;
    } else doDisplayUsage = true;

    // Get the options
    plMode mode             = PL_MODE_CONNECTED;
    const char* buildName   = 0;
    int  threadGroupQty     = 1;
    int  durationMultiplier = 1;
    int  serverConnectionTimeoutMsec = -1;
    int  argCount           = 2;

    while(!doDisplayUsage && argCount<argc) {
        const char* w = argv[argCount];
        if     ( strcasecmp(w, "--n")==0 || strcasecmp(w, "-n")==0) mode = PL_MODE_INACTIVE;
        else if( strcasecmp(w, "--f")==0 || strcasecmp(w, "-f")==0) mode = PL_MODE_STORE_IN_FILE;
        else if((strcasecmp(w, "--b")==0 || strcasecmp(w, "-b")==0) && argCount+1<argc) {
            buildName = argv[++argCount];
            printf("Build name is: %s\n", buildName);
        }
        else if(strcasecmp(w, "--port")==0 && argCount+1<argc) {
            int serverPort = strtol(argv[++argCount], 0, 10);
            printf("Socket port: %d\n", serverPort);
            plSetServer("127.0.0.1", serverPort);
        }
        else if((strcasecmp(w, "-t")==0 || strcasecmp(w, "--t")==0) && argCount+1<argc) {
            threadGroupQty = strtol(argv[++argCount], 0, 10);
            printf("Thread group qty: %d\n", threadGroupQty);
            if(threadGroupQty<1 || threadGroupQty>9) {
                printf("Error: the thread group quantity shall be in [1;9]\n");
                doDisplayUsage = true;
            }
        }
        else if((strcasecmp(w, "-w")==0 || strcasecmp(w, "--w")==0) && argCount+1<argc) {
            serverConnectionTimeoutMsec = strtol(argv[++argCount], 0, 10);
            printf("Server connection timeout: %d ms\n", serverConnectionTimeoutMsec);
        }
        else if((strcasecmp(w, "-l")==0 || strcasecmp(w, "--l")==0) && argCount+1<argc) {
            durationMultiplier = strtol(argv[++argCount], 0, 10);
            if(durationMultiplier<=0) {
                printf("Error: the duration multiplier shall be a strictly positive integer\n");
                doDisplayUsage = true;
            }
            printf("Duration multiplier: %d\n", durationMultiplier);
        }
        else {
            printf("Error: unknown argument '%s'\n", w);
            doDisplayUsage = true;
        }
        ++argCount;
    }

    // Sanity checks
    if(threadGroupQty<=0 || durationMultiplier<=0) doDisplayUsage = true;

    // Some display
    if(doDisplayUsage) {
        displayUsage(argv[0]);
        return 1;
    }
    switch(mode) {
    case PL_MODE_CONNECTED:     printf("Mode 'connected'\n"); break;
    case PL_MODE_STORE_IN_FILE: printf("Mode 'file storage'\n"); break;
    case PL_MODE_INACTIVE:      printf("Mode 'inactive'\n"); break;
    default:                    plAssert(0, "This case is not possible");
    }

    // Set the record filename (used only in case for file storage mode)
    plSetFilename("example_record.pltraw");

    if(behavior==PERF) {
        // Estimate the cost of the logging
        evaluatePerformance(mode, buildName, durationMultiplier, serverConnectionTimeoutMsec);
    }
    else {
        // Collect events for a multi-threaded test program
        // The purposes are:
        //  - to show an example of instrumentation
        //  - to test all instrumentation APIs
        collectInterestingData(mode, buildName, durationMultiplier, threadGroupQty, crashKind, serverConnectionTimeoutMsec);
    }

    return 0;
}
