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

// This file is the second one for the C++ instrumentation test program
// Why is the test program split into two files?
// Simply to validate that having some instrumentation in a file without "PL_IMPLEMENTATION" works well too.


// System includes
#include <cmath>
#include <cstring>

// Local includes
#include "palanteer.h"
#include "testPart.h"

// Selection of the logging or not of the "RANDOM" group
#ifndef PL_GROUP_RANDOM
#define PL_GROUP_RANDOM 1
#endif



// =============
// Crash helpers
// =============

// Inline just to stress & test the stacktrace display
template<class T>
inline
void
crashSubContractor(uint16_t crashKind, T zero)
{
    switch(crashKind) {
    case 0: printf("%d", 1/zero); break;
    case 1: *((volatile char*)0) = 0; break;
    case 2:
        plAssert(0==1, "This is an assertion-based crash", zero, crashKind);
        (void) zero; // Fallthrough, in case NDEBUG is defined
        // FALLTHRU
    case 3: abort();
    }
}


void
doCrash_Please(int crashKind, int zero)
{
    crashSubContractor(crashKind, zero);
}


// =======================
// Random number generator
// =======================

uint32_t
RandomLCM::get(uint64_t min, uint64_t max)
{
    plScope("RandomLCM::get");
    plgLockWait(RANDOM, "Random generator");
    std::lock_guard<std::mutex> lk(_mx);
    plgLockScopeState(RANDOM, "Random generator", true);

    uint32_t generatedNumber = (uint32_t)(min+(getNext()&0xFFFFFFFF)*(max-min)/(1LL<<32));
    plgVar(RANDOM, generatedNumber);

    return generatedNumber;
}

uint64_t
RandomLCM::getNext(void)
{
    // It is a really dummy random generator
    uint64_t x = _lastValue;
    x ^= x<<13;
    x ^= x>>17;
    x ^= x<<5;
    _lastValue = (_lastValue^x)*1099511628211ULL;
    return x;
}


// ===================================
// Functions of the "associated" task
// ===================================

float
busyWait(int kRoundQty)
{
    float cosSum = 14.;
    for(int i=0; i<1000*kRoundQty; ++i) cosSum += cos(0.1*i);
    return cosSum;
}


// Thread entry point
void
associatedTask(int groupNbr, const char* groupName, int crashKind)
{
    // Declare the thread name with a dynamic string
    char fullThreadName[64];
    snprintf(fullThreadName, sizeof(fullThreadName), "%s%sAssociate", groupName, strlen(groupName)? "/":"");
    plDeclareThreadDyn(fullThreadName);

    float dummyValue   = 0.;
    int   iterationNbr = 0;
    Synchro& synchro   = groupSynchro[groupNbr];
    std::string synchroLockName = strlen(groupName)? (std::string(groupName) + " synchro") : "synchro";

    while(1) {
        // Local scope to contain the RAII lock
        {
            // Wait for an order to do something
            plLockWaitDyn(synchroLockName.c_str());
            std::unique_lock<std::mutex> lk(synchro.mx);
            synchro.cv.wait(lk, [&synchro] { return synchro.command.load()!=0; });

            // Thread was awakened
            plLockScopeStateDyn(synchroLockName.c_str(), true);

            // Get the command from the "control" thread of the group
            int controlCount = synchro.command.exchange(0);
            plLockStateDyn(synchroLockName.c_str(), false);
            if(controlCount==2) break; // End of thread, command "2" from the control thread means quit the while loop
        }

        // Marker of a great event
        if(iterationNbr==4) plMarker("important", "5th iteration reached!");

        // Do something
        plScope("SomeWork");
        dummyValue += busyWait(globalRandomGenerator.get(1500, 4000));

        // Crash if required
        if(crashKind>=0 && iterationNbr==3) doCrash_Please(crashKind, 0); // Crash at 3rd iteration if crash required
        ++iterationNbr;
    }

    plBegin("Final result");
    plVar(dummyValue);
    plEnd("");
}



#if USE_PL==1 && PL_VIRTUAL_THREADS==1

// ================================
// Functions of the "Fiber" tasks
// ================================

// Fibers are shared
std::mutex fibersMx;


// Thread entry point
void
fiberWorkerTask(int workerThreadNbr, std::vector<Fiber>* fiberPool, std::vector<Fiber>* fiberWaitingList, std::atomic<int>* sharedJobIndex)
{
    // This tasks stimulates the 3 required API for enabling support of virtual threads:
    // 1) plDeclareVirtualThread, to associate the external thread ID to a name
    // 2) plAttachVirtualThread,  to attach a virtual threads to the current worker thread
    // 3) plDetachVirtualThread,  to detach the virtual thread. The thread is back to the OS thread

    // Declare the worker thread name with a dynamic string
    char tmpStr[64];
    snprintf(tmpStr, sizeof(tmpStr), "Fiber workers/Fiber worker %d", workerThreadNbr+1);
    plDeclareThreadDyn(tmpStr);

    // Log on the OS thread
    plMarker("threading", "Fiber worker thread creation");

    // Same job definition on all workers
    constexpr int JOBS_QTY = 6;
    const char* jobNames[JOBS_QTY] = { "Load texture", "Update particules", "Animate chainsaw",
                                       "Skeleton interpolation", "Fog of War generation", "Free arena memory pools" };

    int iterationNbr = 0;
    while(iterationNbr<50 || !fiberWaitingList->empty()) {
        ++iterationNbr;
        Fiber fiber;

        // Dice roll
        int dice = globalRandomGenerator.get(0, 99);

        // 1/3 chance to resume a waiting job (unless we are at the end)
        if(!fiberWaitingList->empty() && (dice<33 || iterationNbr>=20)) {
            fibersMx.lock();
            if(fiberWaitingList->empty()) {
                fibersMx.unlock();
                continue;
            }
            // Take a random waiting fiber
            int idx = globalRandomGenerator.get(0, fiberWaitingList->size());
            fiber = (*fiberWaitingList)[idx];
            fiberWaitingList->erase(fiberWaitingList->begin()+idx);
            fibersMx.unlock();
        }

        // 1/4 chance to idle
        else if(dice>75) {
            std::this_thread::sleep_for(std::chrono::milliseconds(globalRandomGenerator.get(10, 30)));
            continue;
        }

        // Else take a new job if some fibers are available
        else {
            fibersMx.lock();
            if(fiberPool->empty()) {
                fibersMx.unlock();
                continue;
            }
            // Pick a fiber
            fiber = fiberPool->back(); fiberPool->pop_back();
            fibersMx.unlock();
            fiber.currentJobId = -1;
        }
        // From here, we have a fiber (to start using or interrupted)

        // Give a name to this fiber
        if(!fiber.isNameAlreadyDeclared) {
            snprintf(tmpStr, sizeof(tmpStr), "Fibers/Fiber %d", fiber.id);
            plDeclareVirtualThread(fiber.id, tmpStr);  // ==> Second API under check
            fiber.isNameAlreadyDeclared = true;
        }

        // Switch to this "fiber"
        plAttachVirtualThread(fiber.id);  // ==> First API under check

        // Job start?
        bool doEndJob = true;
        if(fiber.currentJobId<0) {
            // Refill by picking the next job
            fiber.currentJobId = (sharedJobIndex->fetch_add(1)%JOBS_QTY);

            // And start it
            plBeginDyn(jobNames[fiber.currentJobId]);
            std::this_thread::sleep_for(std::chrono::milliseconds(globalRandomGenerator.get(10, 30)));
            plData("Worker Id", workerThreadNbr+1);
            plData("Fiber-job Id", fiber.currentJobId);

            // Dice roll: 60% chance to end the job without interruption. Else it will go on the waiting list
            doEndJob = (globalRandomGenerator.get(0, 99)>40);
            plData("Scheduling", doEndJob? "One chunk" : "Interrupted");
        }

        if(doEndJob) {
            // End the job
            std::this_thread::sleep_for(std::chrono::milliseconds(globalRandomGenerator.get(10, 30)));
            plEndDyn(jobNames[fiber.currentJobId]);
            fiber.currentJobId = -1;

            // Put back the fiber in the pool
            fibersMx.lock();
            fiberPool->push_back(fiber);
            fibersMx.unlock();
            plDetachVirtualThread(false); // Third API to check
        }
        else {
            // Interrupt the job, put the fiber on the waiting list
            fibersMx.lock();
            fiberWaitingList->push_back(fiber);
            fibersMx.unlock();
            plDetachVirtualThread(true); // Switch back to the OS thread
        }

    } // End of loop on iterations

    plDetachVirtualThread(false); // Switch back to the OS thread
    plMarker("threading", "Fiber worker thread end");
}


#endif
