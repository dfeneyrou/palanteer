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


# Palanteer: Decoder of masked text with the external strings feature
#
# What it does:
#  - loads the lookup hash->string
#  - reads text from stdin, replaces detected hashed strings
#  - outputs decoded text on stdout


import sys
if sys.version_info.major<3:
    print("ERROR: This tool requires python3 (not python2)", file=sys.stderr)
    sys.exit(1)
import re


# Main entry
# ==========

def main(argv):

    # Help
    if len(argv)!=2:
        print("This tool is a part of Palanteer and useful for the 'external string' feature.", file=sys.stderr)
        print("It takes one argument, the 'external string' file lookup, replaces detected hashed strings from stdin and outputs the decoded version in stdout.", file=sys.stderr)
        print("\n  Syntax: %s <string file lookup>\n" % argv[0], file=sys.stderr)
        print("Typical usage is decoding assertion messages.")
        print("Example: cat 'pointer @@894920843EBC824C@@' | %s myHashedStringLookup" % argv[0])
        print("Example: xclip -o | %s myHashedStringLookup" % argv[0])
        sys.exit(1)

    # Load the lookup
    with open(argv[1], 'r') as fHandle: lines = fHandle.readlines()
    lkup = { }
    MATCH_LKUP_ENTRY = re.compile("@@([0-9A-F]{16})@@(.*)")
    for l in lines:
        m = MATCH_LKUP_ENTRY.match(l)
        if m: lkup[m.group(1)] = m.group(2)

    # Read stdin, replace hash string patterns and dump on stdout
    MATCH_INPUT_ENTRY = re.compile("(.*?)@@([0-9A-F]{16})@@(.*)$")
    for l in sys.stdin.readlines():
        outLine = [ ]
        m = MATCH_INPUT_ENTRY.match(l)
        while m:
            outLine.append(m.group(1))
            outLine.append(lkup.get(m.group(2), m.group(2)))
            l = m.group(3)
            m = MATCH_INPUT_ENTRY.match(l)
        outLine.append(l)
        print("".join(outLine))

    sys.exit(0)


# Bootstrap
if __name__ == "__main__":
    main(sys.argv)
