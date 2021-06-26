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

#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>


// Synchronization
// ===============
struct Synchro {
    Synchro(void) { command.store(0); }
    Synchro(const Synchro& s) { command.store(s.command.load()); }
    std::atomic<int> command;
    std::mutex       mx;
    std::condition_variable cv;
};

extern std::vector<Synchro> groupSynchro;


// Random generator
// ================
class RandomLCM
{
public:
    RandomLCM(void) { }
    uint32_t get(uint64_t min, uint64_t max);

private:
    uint64_t   getNext(void);
    uint64_t   _lastValue = 14695981039346656037ULL;
    std::mutex _mx;
};

extern RandomLCM globalRandomGenerator;


// Functions
// =========
void  associatedTask(int groupNbr, const char* groupName, int crashKind);

float busyWait(int kRoundQty);

#if USE_PL==1 && PL_VIRTUAL_THREADS==1

struct Fiber {
    int  id;
    int  currentJobId = -1; // -1 means none
    bool isNameAlreadyDeclared = false;
};

void fiberWorkerTask(int workerThreadNbr, std::vector<Fiber>* fiberPool, std::vector<Fiber>* fiberWaitingList,
                     std::atomic<int>* sharedJobIndex);
#endif
