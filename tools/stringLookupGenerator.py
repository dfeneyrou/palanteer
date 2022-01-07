#! /usr/bin/env python3

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


# Palanteer: Generate an external strings lookup from C++ code
#            and/or from a binary (for the automatic instrumentation case)
#
# What it does for source code filenames:
#  - loop on all the provided C/C++ files
#   - identify Palanteer calls and corresponding parameters + file basenames
#   - compute and display the tuple <key> <string> on stdout
#
# What it does for a provided binary:
#  - calls "nm" on the Linux elf binary
#  - collect all symbols from the text section
#  - format and display the tuples <key> <string> on stdout (2 per function: filename and functio

import sys

if sys.version_info.major < 3:
    print("ERROR: This tool requires python3 (not python2)", file=sys.stderr)
    sys.exit(1)
import os
import os.path
import re
import subprocess


# Constants
# =========

# Regexp to detect if a word which starts with pl[g] (so that it looks like a command) followed with a parenthesis
MATCH_DETECT = re.compile(".*?(^|[^a-zA-Z\d])pl(g?)([a-zA-Z]*)\s*\((.*)")
# Regexp to extract the symbols from the text section, and also the weak ones
MATCH_INFO_LINE = re.compile("^([0-9a-z]+)\s+[TW]\s(.*?)\s(\S+):(\d+)$", re.IGNORECASE)

# Commands whose parameters shall be processed. Associated values are: 0=convert only strings   1=convert all parameters
PL_COMMANDS_TYPE = {
    "Assert": 1,
    "Begin": 0,
    "End": 0,
    "Data": 0,
    "Text": 0,
    "DeclareThread": 0,
    "LockNotify": 0,
    "LockNotifyDyn": 0,
    "LockWait": 0,
    "LockWaitDyn": 0,
    "LockState": 0,
    "LockStateDyn": 0,
    "LockScopeState": 0,
    "LockScopeStateDyn": 0,
    "MakeString": 0,
    "Marker": 0,
    "MarkerDyn": 0,
    "LogDebug": 0,
    "LogInfo": 0,
    "LogWarn": 0,
    "LogError": 0,
    "MemPush": 0,
    "RegisterCli": 1,
    "Scope": 0,
    "Text": 0,
    "Var": 1,
}

# Helpers
# =======


def computeHash(s, isHash64bits, hashSalt):
    if isHash64bits:
        h = 14695981039346656037 + hashSalt
        for c in s:
            h = ((h ^ ord(c)) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        if h == 0:
            h = 1  # Special case for our application (0 is reserved internally)
        return h
    else:
        h = 2166136261 + hashSalt
        for c in s:
            h = ((h ^ ord(c)) * 16777619) & 0xFFFFFFFF
        if h == 0:
            h = 1  # Special case for our application (0 is reserved internally)
        return h


def addString(
    s, hashToStringlkup, collisions, isHash64bits, hashSalt, h=None, doOverride=False
):
    if h == None:
        h = computeHash(s, isHash64bits, hashSalt)
    if h in hashToStringlkup:
        # String already in the lookup?
        if s == hashToStringlkup[h]:
            return
        # Collision!
        if not doOverride:
            if h in collisions:
                collisions[h].append(s)
            else:
                collisions[h] = [hashToStringlkup[h], s]
            return
    # Add the new entry to the lookup
    hashToStringlkup[h] = s


# Main entry
# ==========


def main(argv):

    # Command line parameters parsing
    # ===============================
    hashSalt = 0
    isHash64bits = True
    doPrintUsage = False
    exeNames = []
    fileNames = []
    i = 1
    while i < len(argv):
        if argv[i] == "--hash32":
            isHash64bits = False
        elif argv[i] == "--salt" and i + 1 < len(argv):
            i = i + 1
            hashSalt = int(argv[i])
        elif argv[i] == "--exe" and i + 2 < len(argv):
            exeNames.append((argv[i + 1], argv[i + 2]))
            i = i + 2
        elif argv[i][0] == "-":
            doPrintUsage = True  # Unknown option
            print("Unknown option '%s'" % argv[i], file=sys.stderr)
        else:
            fileNames.append(argv[i])
        i = i + 1
    if not fileNames and not exeNames:
        doPrintUsage = True
    if exeNames and sys.platform != "linux":
        print(
            "Automatic instrumentation requires the 'nm' tool (Unix)", file=sys.stderr
        )
        doPrintUsage = True

    if doPrintUsage:
        print(
            """This tool is a part of Palanteer and useful for the 'external string' feature.
It parses C++ files and dumps on stdout a hashed string lookup for the Palanteer calls.

Syntax: %s [options] [<source filenames>*]

Options:
  --hash32      : generate 32 bits hash (to use with PL_SHORT_STRING_HASH=1)
  --salt <value>: hash salt value (shall match PL_HASH_SALT, default value is 0)
  --exe  <appName> <name> : Provided appName and ELF executable from which to extract the function names and addresses
                            'appName' must exactly match the name provided as 1st argument of plInitAndStart
                            Useful only when using PL_IMPL_AUTO_INSTRUMENT (Linux only)
                            May be used several times in case of auto-instrumented multistreams
Note 1: The 'hand-made' code parsing for processing external strings is simple but should be enough for most need.
        It may fail in some corner cases (C macro masking values etc...).
Note 2: If Palanteer commands are encapsulated inside custom macros in your code, the list of command
        at the top of this file shall probably be modified.

Ex: %s src/*.cpp thirdParty/*.cc     (external strings)
Ex: %s --exe "name1" bin/myFirstProcess.exe --exe "name2" bin/mySecondProcess.exe  src/*.cpp  (external strings and auto-instrumented multistream)
"""
            % (argv[0], argv[0], argv[0]),
            file=sys.stderr,
        )
        sys.exit(1)

    # Process the executables
    # =======================

    hashToStringlkup, collisions = {}, {}

    for exeParams in exeNames:
        appName, exeName = exeParams
        if not os.path.exists(exeName):
            printf("Input executable '%s' does not exist" % exeName, file=sys.stderr)
            sys.exit(1)

        # Collect the information from 'nm'
        # As the base virtual address is substracted from the logged function address, no need to care about ASLR
        nmProcess = subprocess.run(
            ["nm", "--line-numbers", "--demangle", exeName],
            universal_newlines=True,
            capture_output=True,
        )

        # Get the hash of the appName. This allows seamless usage of auto instrumentation with multiple streams
        appNameHash = computeHash(appName, isHash64bits, hashSalt)

        for l in nmProcess.stdout.split("\n"):
            # Filter on symbols from the text section only
            m = MATCH_INFO_LINE.match(l)
            if not m:
                continue

            # The hash value in this automatic instrumentation case is the base address of the function
            hashValue = int(m.group(1), 16) + hashSalt
            # Add the function name
            addString(
                m.group(2),
                hashToStringlkup,
                collisions,
                isHash64bits,
                hashSalt,
                hashValue ^ appNameHash,
            )

            # Add the function filename, augmented with the line number
            # The override of any former value is due to the potential emission of dual constructor/destructor symbols by GCC/Clang
            # (see https://stackoverflow.com/questions/6921295/dual-emission-of-constructor-symbols for details)
            addString(
                "%s:%s" % (m.group(3), m.group(4)),
                hashToStringlkup,
                collisions,
                isHash64bits,
                hashSalt,
                (hashValue + 1) ^ appNameHash,
                doOverride=True,
            )

    # Process source files
    # ====================

    addString(
        "", hashToStringlkup, collisions, isHash64bits, hashSalt
    )  # Add the empty string which are used internally

    for f in fileNames:
        # Insert the file basename, as it is what the palanteer client is using
        basename = os.path.basename(f)
        addString(basename, hashToStringlkup, collisions, isHash64bits, hashSalt)

        # Load the file
        lines = []
        with open(f, "r") as fHandle:
            lines = fHandle.readlines()

        # Loop on lines
        lineNbr, lineQty = 0, len(lines)
        while lineNbr < lineQty:

            # Detect a Palanteer command
            l = lines[lineNbr]
            while l:
                m = MATCH_DETECT.match(l)
                if not m:
                    l, lineNbr = None, lineNbr + 1
                    continue
                cmdType = PL_COMMANDS_TYPE.get(m.group(3), None)
                if cmdType == None:
                    l = m.group(4)
                    continue
                isGroup = not not m.group(2)

                # Parse the parameters
                params, currentParam, isInQuote, parenthLevel, prevC = (
                    [],
                    [],
                    False,
                    0,
                    "",
                )
                paramLine = m.group(4)
                while parenthLevel >= 0:
                    for i, c in enumerate(paramLine):
                        if c == '"':
                            isInQuote = not isInQuote
                        if isInQuote:
                            currentParam.append(c)
                            continue
                        if c == "/" and prevC == "/":
                            currentParam = currentParam[:-1]  # Remove first '/'
                            break
                        elif c == "(":
                            parenthLevel += 1
                            currentParam.append(c)
                        elif c == ")":
                            parenthLevel -= 1
                            if parenthLevel < 0:  # Palanteer command terminaison
                                params.append("".join(currentParam).strip())
                                l = paramLine[i:]
                                break
                            else:
                                currentParam.append(c)
                        elif c == "," and parenthLevel == 0:
                            params.append("".join(currentParam).strip())
                            currentParam = []
                        else:
                            currentParam.append(c)
                        prevC = c

                    # If parsing is not complete, process next line
                    if parenthLevel >= 0:
                        lineNbr += 1
                        if lineNbr < lineQty:
                            paramLine = lines[lineNbr]
                        else:
                            break

                # Update the lookup
                if isGroup:
                    params = params[1:]  # Remove group parameter
                for p in params:
                    if not p:
                        continue
                    if p[0] == '"' and p[-1] == '"':
                        addString(
                            p[1:-1],
                            hashToStringlkup,
                            collisions,
                            isHash64bits,
                            hashSalt,
                        )
                    # Commands of type 1 stringifies all parameters. Also with quote (for assertions)
                    if cmdType == 1:
                        addString(
                            p, hashToStringlkup, collisions, isHash64bits, hashSalt
                        )

    # Output
    sortedK = sorted(hashToStringlkup.keys(), key=lambda x: hashToStringlkup[x].lower())
    for k in sortedK:
        print("@@%016X@@%s" % (k, hashToStringlkup[k]))

    # Error
    if collisions:
        sortedK = sorted(collisions.keys())
        for k in sortedK:
            cList = collisions[k]
            print(
                "COLLISION %016X %s"
                % (k, " ".join(["[%s]" % s for s in collisions[k]])),
                file=sys.stderr,
            )

    # Exit status
    sys.exit(1 if collisions else 0)


# Bootstrap
if __name__ == "__main__":
    main(sys.argv)


# Unit test
# =========

# ./stringLookupGenerator.py stringLookupGenerator.py  shall give "good" followed by a sequential numbers, and no "BAD"
"""
plBegin  aa("BAD0");

plBegin("good01");
plBegin ("good02");
plBegin("good03", "good04");
plgBegin(BAD1, "good05");
plBegin("good06",
        "good07") "BAD2";
plBegin("good08", // "BAD3"
        "good09"); // "BAD4"
plVar(good10, good11);
plgVar (BAD5, good12,
        good13);
plgVar (BAD6, good14, // BAD7
        good15);
plAssert(good16);
plgAssert(BAD8, good17,
          good18);
plAssert(good19(a,b()), good20("content("), good21);

not at start of the line plMakeString("good22"),plMakeString ("good23" ) , plMakeString ( "good24")

plBegin("good25 <<< last one"); // Easy one at the end so it is easy to detect non sequential "goods"
"""
