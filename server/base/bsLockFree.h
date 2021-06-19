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

#include <atomic>

/* "Lock free" structure to send one message at a time across 2 threads, one sender and one receiver.
   The message can be re-sent by sender only when previous message is fully processed by receiver.

   Class <T> shall have a default constructor, called at init,
     and shall be cleared/rebuild at update time.

   Thread1: "Sender" thread
            Get free structure if available, fills it, and send it
   Thread2: "Receiver" thread
            Check if a structure msg has been sent and, if it is the case, lock it for processing
            then release it for another message cycle
 */
template <class T>
class bsMsgExchanger {
public:
    bsMsgExchanger<T>(void) {
        _free.store(new T());
        _sent.store(0);
    }
    ~bsMsgExchanger<T>(void) {
        delete _free.exchange(0);
        delete _sent.exchange(0);
        delete _receivedMsg;
    }
    T* getRawData(void) { // Optional. Used for initialization for instance
        return _free.load();
    }

    // Methods for sending thread
    T* t1GetFreeMsg(void) { // Returned value may be null
        return _free.load();
    }
    void t1Send(void) {
        T* toSend = _free.exchange(0);
        plAssert(toSend);
        T* empty = _sent.exchange(toSend);
        plAssert(!empty);
    }
    // Methods for receiving thread
    T* getReceivedMsg(void) {
        plAssert(!_receivedMsg);
        _receivedMsg = _sent.exchange(0);
        return _receivedMsg;
    }
    void releaseMsg(void) {
        plAssert(_receivedMsg);
        _receivedMsg = _free.exchange(_receivedMsg);
        plAssert(!_receivedMsg);
    }

private:
    bsMsgExchanger(const bsMsgExchanger<T>& other); // To please static analyzers
    bsMsgExchanger<T>& operator=(bsMsgExchanger<T> other);

    std::atomic<T*> _free; // Starts filled (other is empty)
    std::atomic<T*> _sent;
    T* _receivedMsg = 0;
};


/* "Lock free" structure push transfer across 2 threads, 1 pusher and 1 user.
   Several structure updates in thread 1 without switch on thread2 is ok. Only the last updated one is seen
   by thread 2 when it is ready to retrieve it.

   Class <T> shall have a default constructor, called at init,
     and shall be cleared/rebuild at update time.

   Thread1: Data "pusher" thread
            Get a free structure, fills it with the data, and set it as the next structure to push
   Thread2: Data "user" thread
            In a cycle:
             - If a "next" structure is present, swap it with the current one in use.
             - Use the current structure
 */
template <class T>
class bsPushData {
public:
    bsPushData<T>(void) {
        _free.store(new T());
        _free2.store(new T());
        _nextUsed.store(0);
        _curUsed = new T();
    }
    ~bsPushData<T>(void) {
        delete _free.exchange(0);
        delete _free2.exchange(0);
        delete _nextUsed.exchange(0);
        delete _curUsed;
    }
    //vector<T*> getRawDataList(void) { // Optional. Used for initialization for instance. One of them  will be null
    //    return vector<T*> { _free.load(), _free2.load(), _nextUsed.load(), _curUsed};
    //}

    // Methods for data updating thread
    T* t1GetFree(void) { // To give back with setNextUsed(...)
        T* t     = _free .exchange(0);
        if(!t) t = _free2.exchange(0);
        plAssert(t);
        return t;
    }
    void t1SetNextUsed(T* nextToUse) {
        T* prevNext = _nextUsed.exchange(nextToUse);
        if(prevNext) {
            prevNext = _free.exchange(prevNext);
            if(prevNext) prevNext = _free2.exchange(prevNext);
            plAssert(!prevNext);
        }
    }
    // Methods for data user thread
    bool updateUsed(void) {
        if(!_nextUsed.load()) return false;
        T* tmp = _curUsed;
        _curUsed = _nextUsed.exchange(0);
        tmp = _free.exchange(tmp);
        if(tmp) tmp = _free2.exchange(tmp);
        plAssert(!tmp);
        return true;
    }
    T* getUsed(void) {
        plAssert(_curUsed);
        return _curUsed;
    }

private:
    bsPushData(const bsPushData<T>& other); // To please static analyzers
    bsPushData<T>& operator=(bsPushData<T> other);

    std::atomic<T*> _free;
    std::atomic<T*> _free2;
    std::atomic<T*> _nextUsed; // Starts empty (others are filled)
    T*              _curUsed;
};
