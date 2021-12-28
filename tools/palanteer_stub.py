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

# This file is a stub for the Palanteer Python instrumentation module in case the
# instrumentation library is not installed.
# The usage is to import the Palanteer instrumentation module this way:
#   try:
#     import palanteer
#   except ModuleNotFoundError:
#     import palanteer_stub as palanteer  # Fallback to empty functions


def plFunction(func):
    return func


def plInitAndStart(*args, **kwargs):
    pass


def plStopAndUninit():
    pass


def plDeclareThread(name):
    pass


def plData(name, value):
    pass


def plText(name):
    pass


# Deprecated function
def plMarker(category, msg):
    pass


def plLogDebug(category, msg):
    pass


def plLogInfo(category, msg):
    pass


def plLogWarn(category, msg):
    pass


def plLogError(category, msg):
    pass


def plLockWait(name):
    pass


def plLockState(name, state):
    pass


def plLockNotify(name):
    pass


def plBegin(name):
    pass


def plEnd(name=None):
    pass


def plRegisterCli(handler, name, spec, descr):
    pass


def plFreezePoint():
    pass


def plSetLogLevelRecord(logLevel):
    pass


def plSetLogLevelConsole(logLevel):
    pass
