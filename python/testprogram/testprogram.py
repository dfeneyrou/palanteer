#! /usr/bin/python3

# The MIT License (MIT)
#
# Copyright(c) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files(the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions :
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import sys
import time
import math
import threading
import asyncio

# If palanteer module is not found, it imports the stub (in ./tools) which defines all APIs as no-op
# This way, a program can be distributed without any Palanteer installation requirement
try:
    from palanteer import *
except ModuleNotFoundError:
    from palanteer_stub import *


# This file is a test program with multiple purposes:
#  - show an example of Python instrumentation
#  - have a way to measure speed performance in a specific case
#  - be a part of Palanteer internal tests, by using all instrumentation APIs and features


# =======================
# Random number generator
# =======================


class RandomLCM:
    def __init__(self):
        self.lastValue = 14695981039346656037
        self.mx = threading.Lock()

    @plFunction
    def get(self, minValue, maxValue):
        plLockWait("Random generator")
        self.mx.acquire()
        plLockState("Random generator", True)
        generatedNumber = int(
            minValue + (self._next() * (maxValue - minValue) / (1 << 32))
        )
        plLockState("Random generator", False)
        self.mx.release()
        plData("Number", generatedNumber)
        return generatedNumber

    def _next(self):
        # It is a really dummy random generator
        x = self.lastValue
        x ^= x << 13
        x ^= x >> 17
        x ^= x << 5
        self.lastValue = ((self.lastValue ^ x) * 1099511628211) & 0x7FFFFFFFFFFFFFFF
        return x & 0xFFFFFFFF


# ==============================
# Global context
# ==============================


class GroupSynchro:
    def __init__(self, name):
        self.name = "%ssynchro" % name.replace("/", " ")
        self.mx = threading.Lock()
        self.cv = threading.Condition(self.mx)
        self.message = None


globalSharedMx = threading.Lock()
globalRandomGenerator = RandomLCM()

# =============
# Crash helpers
# =============


def crashSubContractor(crashKind):
    if crashKind == 0:
        printf("%d", 1 / zero)
    elif crashKind == 1:
        a = range(5)[6]
    elif crashKind == 2:
        assert 0, "This is an assertion-based crash" % (zero, crashKind)
    elif crashKind == 3:
        sys.exit(1)


def doCrash_Please(crashKind):
    crashSubContractor(crashKind)


# ===================================
# Functions of the "associated" task
# ===================================


def busyWait(kRoundQty):
    cosSum = 14.0
    for i in range(100 * kRoundQty):
        cosSum += math.cos(0.1 * i)
    return cosSum


# Thread entry point
def associatedTask(synchro, crashKind):
    plDeclareThread(threading.current_thread().name)

    dummyValue = 0.0
    iterationNbr = 0

    while 1:
        # Wait for an order to do something
        plLockWait(synchro.name)
        synchro.mx.acquire()
        synchro.cv.wait_for(lambda x=synchro: synchro.message != None)
        plLockState(synchro.name, True)

        # Get the command from the "control" thread of the group
        command, synchro.message = synchro.message, None
        plLockState(synchro.name, False)
        synchro.mx.release()

        if command == "stop":
            break  # End of thread

        # Marker of a great event
        if iterationNbr == 4:
            plMarker("important", "5th iteration reached!")

        # Do something
        plBegin("SomeWork")
        dummyValue += busyWait(globalRandomGenerator.get(1500, 4000))

        # Crash if required
        if crashKind >= 0 and iterationNbr == 3:
            doCrash_Please(crashKind)  # Crash at 3rd iteration if crash required

        iterationNbr += 1
        plEnd("SomeWork")

    plBegin("Final result")
    plData("Dummy value", dummyValue)
    plEnd("Final result")


# ==============================
# Functions of the "control" task
# ==============================


@plFunction
def otherSubTask(taskNbr, iterNbr):
    plData("taskNbr", taskNbr)
    plData("iterNbr", iterNbr)

    # Allocate something
    dummyAlloc = [1] * globalRandomGenerator.get(1000, 5000)

    # Compute something
    dummyValue = busyWait(globalRandomGenerator.get(500, 1000))

    # Deallocate (no real effect in Python as objects go back to internal object pools)
    dummyAlloc = None

    plBegin("doSomethingUseful")
    dummyValue += busyWait(globalRandomGenerator.get(100, 500))
    for i in range((7 * taskNbr * iterNbr) % 3):
        plBegin("Partial work")
        dummyValue += busyWait(globalRandomGenerator.get(100, 500))
        plEnd("Partial work")
    plEnd("doSomethingUseful")

    # Log something visual
    x = 0.2 * (0.25 * taskNbr + iterNbr) + 0.1
    plData("exp(x)/x", math.exp(x) / x)

    return dummyValue


@plFunction
def subTaskUsingSharedResource(taskNbr, iterNbr):
    fruits = ["apple", "orange", "durian", "banana", "grenada"]
    vegetables = ["carrot", "onion", "bean", "patato"]
    plData(
        "input value##hexa", taskNbr
    )  # This "hexa" unit is special, this integer value will be displayed in hexadecimal on viewer

    # Compute something
    dummyValue = busyWait(150)

    # Allocate something
    dummyAlloc = [1] * globalRandomGenerator.get(100, 500)

    superList = []
    for i in range(5000):
        plBegin("Add fruit")
        superList.append(fruits[(taskNbr + i * 7) % 5])
        plEnd()

    plData("Last one", superList[-1])
    plData(
        "Ingredient for the soup##ingredient", vegetables[(taskNbr + iterNbr * 7) % 4]
    )  # The unit is declared as "ingredient"

    # Log something visual
    plData(
        "Computation output##parsec", math.cos(1.5 * (0.25 * taskNbr + iterNbr))
    )  # The unit is declared as "parsec"

    return dummyValue


# Thread entry point
@plFunction
def controlTask(synchro, durationMultipler):
    someStrings = ["Even", "Odd"]
    plDeclareThread(threading.current_thread().name)

    iterationQty = 10 * durationMultipler
    dummyValue = 0
    allocationList = []

    plFreezePoint()

    for iterNbr in range(iterationQty):

        if globalRandomGenerator.get(0, 100) >= 45:
            # Allocate a new list
            allocationList.append([1] * globalRandomGenerator.get(2000, 10000))
        else:
            # Deallocate
            if allocationList:
                del allocationList[0]

        # Wait a bit
        time.sleep(0.001 * globalRandomGenerator.get(20, 60))

        # Prepare the work
        plBegin("Iteration")
        plData("iterNbr", iterNbr)
        plData("iterationQty", iterationQty)

        # Dynamic but still external string compatible markers
        plMarker("Count", someStrings[iterNbr % 2])

        taskQty = globalRandomGenerator.get(1, 4)
        dummyValue += busyWait(globalRandomGenerator.get(500, 2500))

        for taskNbr in range(taskQty):
            plBegin("Task")
            plData("Task number", taskNbr)

            # Work with some shared resource
            dummyValue += busyWait(globalRandomGenerator.get(300, 1000))
            plLockWait("Shared resource")
            globalSharedMx.acquire()
            plLockState("Shared resource", True)
            dummyValue += subTaskUsingSharedResource(taskNbr, iterNbr)
            plLockState("Shared resource", False)
            globalSharedMx.release()

            dummyValue += busyWait(globalRandomGenerator.get(10, 200))
            dummyValue += otherSubTask(taskNbr, iterNbr)
            plEnd("Task")

        # Send a signal to the associated task
        synchro.mx.acquire()
        synchro.message = (
            "stop" if (iterNbr == iterationQty - 1) else "work, lazy thread!"
        )
        plLockNotify(synchro.name)
        synchro.cv.notify()
        synchro.mx.release()
        plEnd("Iteration")

    plBegin("Final result")
    plData("Dummy value", dummyValue)
    plEnd("Final result")

    # End of the thread


# ==============================
# AsyncIO worker task
# ==============================


async def baseFunc():
    busyWait(globalRandomGenerator.get(10, 25))
    await asyncio.sleep(0.01 * globalRandomGenerator.get(1, 3))
    busyWait(globalRandomGenerator.get(10, 25))
    await asyncio.sleep(0.01 * globalRandomGenerator.get(1, 3))


async def loadTexture():
    await baseFunc()


async def updateParticules():
    await baseFunc()


async def animateChainsaw():
    await baseFunc()


async def skeletonInterpolation():
    await baseFunc()


async def fogOfWarGeneration():
    await baseFunc()


async def freeArenaMemoryPools():
    await baseFunc()


async def asyncRunner():
    jobKinds = (
        loadTexture,
        updateParticules,
        animateChainsaw,
        skeletonInterpolation,
        fogOfWarGeneration,
        freeArenaMemoryPools,
    )
    for i in range(30):
        await jobKinds[globalRandomGenerator.get(0, len(jobKinds))]()
        time.sleep(0.01)


async def asyncWaitAllTasks():
    await asyncio.gather(*(asyncRunner() for i in range(3)))


def asyncWorkerTask():
    asyncio.run(asyncWaitAllTasks())


# ==============================
# CLI handlers
# ==============================


def delayedAssertThread(condValue):
    plDeclareThread("Crash thread")
    time.sleep(1.0)
    assert condValue, "Assertion called by CLI"
    return 0


def cliHandlerAsyncAssert(condValue):
    threading.Thread(target=lambda x=condValue: delayedAssertThread(x)).start()
    return 0


def cliHandlerCreateMarker(msg):
    plMarker("test_marker", msg)
    return 0


def cliHandlerPoetryGetter():
    return 0, "To bug, or not to bug, that is the question"


def cliHandlerWithParameters(param1, param2, param3):
    # "Complex" handling in order to stimulate important parts of the API
    if param1 <= -1000:
        return (
            1,
            "This text will not be erased\nError: Very negative first parameter. Not great.",
        )
    elif param1 <= -100:
        return 1, "Error: Mildly negative first parameter. Not great."
    elif param1 <= 0:
        return 1, "Error: First parameter shall be strictly positive (%d seen)" % param1

    # Build the response
    response = "Strictly positive integer value is: %d\n" % param1
    response += "Float value is: %f\n" % param2
    response += "String value is: %s\n" % param3
    return 0, response


def cliHandlerQuit():
    sys.exit(0)


# ==============================
# Event collection program
# ==============================


def collectInterestingData(
    mode,
    buildName,
    durationMultiplier,
    serverPort,
    with_c_calls,
    threadGroupQty,
    crashKind,
):

    # Start the logging
    startSec = time.time()
    if mode != "inactive":
        plInitAndStart(
            "Python example",
            record_filename="example_record.pltraw" if mode == "file storage" else None,
            build_name=buildName,
            server_port=serverPort,
            with_c_calls=with_c_calls,
        )

    # Give a name to this thread (after the library initialization)
    plDeclareThread("Main")

    # CLI registration
    # On purpose *after* the call to plInitAndStart in order to better test the freeze point.
    # Reminder: it is recommended to register them *before* the Palanteer initialization in order to remove any race condition
    #           in remote script about calling a not yet registered CLI after connection
    plRegisterCli(
        cliHandlerWithParameters,
        "test::parameters",
        "first=int second_param=float third=string",
        "Uses the 3 types of parameters",
    )
    plRegisterCli(
        cliHandlerWithParameters,
        "test::parametersDft",
        "first=int[[31415926]] second_param=float[[-3.14159265359]] third=string[[no string provided]] fourth=int[[0]]",
        "Uses the 3 types of parameters with default values and a 4th one",
    )
    plRegisterCli(
        cliHandlerAsyncAssert,
        "async_assert",
        "condvalue=int",
        "Call asynchronously an assertion with the provided value after a 1s timeout",
    )
    plRegisterCli(
        cliHandlerCreateMarker,
        "test::marker",
        "msg=string",
        "Create a marker with the provided string",
    )
    plRegisterCli(cliHandlerPoetryGetter, "get_poetry", "", "Returns some poetry.")
    plRegisterCli(cliHandlerQuit, "quit", "", "Exit the program")

    # Freeze points just before starting, and in particular after declaring all CLIs (so that they can be used at this point)
    # These steps are used by Palanter testing
    plFreezePoint()
    plBegin("Freeze control test")
    plData("Freeze", "Before first freeze")
    plFreezePoint()
    plData("Freeze", "After first freeze")
    plFreezePoint()
    plData("Freeze", "After second freeze")
    plEnd("Freeze control test")

    # Launch some active threads
    threadGroupNames = [
        "",
        "Workers/",
        "Real time/",
        "Database Cluster/",
        "Helpers/",
        "Engine/",
        "Compute Grid/",
        "Hub/",
        "Idlers/",
    ]
    crashThreadGroupNbr = (
        None if crashKind == None else int(time.time()) % threadGroupQty
    )  # Random selection of the thread which shall crash

    controlThreadList = []
    for threadGroupNbr in range(threadGroupQty):
        groupName = threadGroupNames[threadGroupNbr]
        groupSynchro = GroupSynchro(groupName)
        t1 = threading.Thread(
            name="%sControl" % groupName,
            target=lambda grp=groupSynchro, durMult=durationMultiplier: controlTask(
                grp, durMult
            ),
        )
        t2 = threading.Thread(
            name="%sAssociate" % groupName,
            target=lambda grp=groupSynchro, ck=crashKind if crashThreadGroupNbr == threadGroupNbr else -1: associatedTask(
                grp, ck
            ),
        )
        t1.start()
        t2.start()
        controlThreadList.append(t1)
        controlThreadList.append(t2)

    # Add some asynchronous jobs (virtual threads / green threads)
    tAsync = threading.Thread(target=asyncWorkerTask)
    tAsync.start()
    controlThreadList.append(tAsync)

    # Wait for threads completion
    for t in controlThreadList:
        t.join()  # Join order does not matter

    plMarker("Threading", "All tasks are completed! Joy!")

    # Stop the recording
    plStopAndUninit()

    # Display the statistics
    durationSec = time.time() - startSec
    print("Statistics:")
    print("  Execution time: %d ms" % (1000.0 * durationSec))


# =========================
# Performance evaluation
# =========================


def evaluatePerformance(mode, buildName, durationMultipler, serverPort):

    # Start the logging
    if mode != "inactive":
        plInitAndStart(
            "Python perf example",
            record_filename="example_record.pltraw" if mode == "file storage" else None,
            build_name=buildName,
            server_port=serverPort,
        )

    # Give a name to this thread (after the library initialization)
    plDeclareThread("Main")

    iterationQty = 250000  # 4 events per loop
    loopQty = iterationQty * durationMultipler
    startCollectSec = time.time()

    # Logging in loop, 4 events per cycle
    for i in range(loopQty):
        plBegin("TestLoop")
        plData("Iteration", i)
        plData("Still to go", loopQty - i - 1)
        plEnd("TestLoop")

    endCollectSec = time.time()
    plStopAndUninit()
    endSendingSec = time.time()

    # Console display
    print(
        "Collection duration : %.2f ms for %d events"
        % (1000.0 * (endCollectSec - startCollectSec), loopQty * 4.0)
    )
    print(
        "Collection unit cost: %.0f ns"
        % (1e9 * (endCollectSec - startCollectSec) / (loopQty * 4.0))
    )
    print(
        "Processing duration : %.2f ms (w/ %s)"
        % (
            1000.0 * (endSendingSec - startCollectSec),
            "disk file writing" if mode == "file storage" else "transmission and server processing",
        )
    )
    print(
        "Processing rate     : %.3f million event/s"
        % (4e-6 * loopQty / (endSendingSec - startCollectSec))
    )


# =========================
# Main
# =========================


def displayUsage():
    print("\nUsage: %s <parameter> [options]" % sys.argv[0])
    print("  Palanteer Python instrumentation test program")
    print("")
    print("  Parameter:")
    print("    'collect'      : Data collection")
    print("    'crash-assert' : Data collection with a planned failed assertion")
    print("    'crash-zerodiv': Data collection with a planned zero division")
    print("    'crash-segv'   : Data collection with a planned seg fault")
    print("    'crash-abort'  : Data collection with a planned abort call")
    print("    'perf'         : Estimation of the logging performances in a loop")
    print("")
    print("  Options to selection the collection mode (exclusive):")
    print("    <Default>: Use remote Palanteer connection")
    print("    '-f'     : Save the record in a file 'example_record.plt'")
    print("    '-n'     : No data collection (event recording not enabled at run time)")
    print("    '-c'     : Do profile the C functions")
    print("")
    print("  Options to configure the program behavior:")
    print(
        "    '-t <1-9>      : Defines the quantity of groups of threads (2 threads per group)"
    )
    print("    '-l'           : Run time length multiplier (default is 1)")
    print(
        "    '-b <name>'    : Provide a build name for the current program (default is none)"
    )
    print("    '--port <port>': Use the provided socket port (default is 59059)")
    print("")
    print("To start, you can try this (and look at the testProgram.cpp code too):")
    print(
        "  %s perf    -f   (no need for palanteer, events are stored in the file example_record.plt) "
        % sys.argv[0]
    )
    print(
        "  %s collect -c   (no need for palanteer, events are displayed on console) "
        % sys.argv[0]
    )
    print(
        "  %s collect      (requires the prior launch of 'palanteer' viewer) "
        % sys.argv[0]
    )


def main():
    # Command line parsing and program behavior selection
    doDisplayUsage, doEstimateCost, crashKind = False, False, None

    # Get the main type of execution
    if len(sys.argv) > 1:
        if sys.argv[1] == "collect":
            pass
        elif sys.argv[1] == "perf":
            doEstimateCost = True
        elif sys.argv[1] == "crash-zerodiv":
            crashKind = 0
        elif sys.argv[1] == "crash-segv":
            crashKind = 1
        elif sys.argv[1] == "crash-assert":
            crashKind = 2
        elif sys.argv[1] == "crash-abort":
            crashKind = 3
        else:
            doDisplayUsage = True
    else:
        doDisplayUsage = True

    # Get the options
    mode, buildName, serverPort, with_c_calls, threadGroupQty, durationMultiplier = (
        "connected",
        None,
        59059,
        False,
        1,
        1,
    )
    argCount = 2
    while not doDisplayUsage and argCount < len(sys.argv):
        w = sys.argv[argCount]
        if w in ["-n", "--n"]:
            mode = "inactive"
        elif w in ["-f", "--f"]:
            mode = "file storage"
        elif w in ["-c", "--c"]:
            with_c_calls = True
        elif w in ["-b", "--b"] and argCount + 1 < len(sys.argv):
            buildName = sys.argv[argCount + 1]
            argCount += 1
            print("Build name is: %s" % buildName)
        elif w == "--port" and argCount + 1 < len(sys.argv):
            serverPort = int(sys.argv[argCount + 1])
            argCount += 1
            print("Socket port: %d" % serverPort)
        elif w in ["-t", "--t"] and argCount + 1 < len(sys.argv):
            threadGroupQty = int(sys.argv[argCount + 1])
            argCount += 1
            print("Thread group qty: %d" % threadGroupQty)
        elif w in ["-l", "--l"] and argCount + 1 < len(sys.argv):
            durationMultiplier = int(sys.argv[argCount + 1])
            argCount += 1
            print("Duration multiplier: %d" % durationMultiplier)
        else:
            print("Error: unknown argument '%s'" % sys.argv[argCount])
            doDisplayUsage = True
        argCount += 1

    # Sanity checks
    if threadGroupQty <= 0 or durationMultiplier <= 0:
        doDisplayUsage = True

    # Display usage and quit
    if doDisplayUsage:
        displayUsage()
        sys.exit(1)
    print("Mode '%s'" % mode)

    if doEstimateCost:
        # Estimate the cost of the logging
        evaluatePerformance(mode, buildName, durationMultiplier, serverPort)
    else:
        # Collect events for a multi-threaded test program
        # The purposes are:
        #  - to show an example of instrumentation
        #  - to test all instrumentation APIs
        collectInterestingData(
            mode,
            buildName,
            durationMultiplier,
            serverPort,
            with_c_calls,
            threadGroupQty,
            crashKind,
        )

    sys.exit(0)


# Bootstrap
if __name__ == "__main__":
    main()
