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


# Palanteer: Standalone tool to manage todos
#
# What it does:
#  - parse all files to collect todo-like annotations
#  - reorganize them (filtering, reordering)
#  - display the todo list

import sys

if sys.version_info.major < 3:
    print("ERROR: This tool requires python3 (not python2)", file=sys.stderr)
    sys.exit(1)
import os
import os.path
import re


# Constants
# =========
MARKER = "@#"  # Feel free to choose your marker here
MATCH_MARKER = re.compile("(.*?)" + MARKER + "([a-z]+?)($|\s)(.*)", re.IGNORECASE)
MATCH_PRIORITY = re.compile("(.*?)(^|\s)P([\.\-0-9]+)($|\s)(.*)")
MATCH_WORKLOAD = re.compile("(.*?)(^|\s)W([\.0-9]+)($|\s)(.*)")
MATCH_CATEGORY = re.compile("(.*?)\s?\[(.*?)\]\s?(.*)")

USAGE = r"""This tool is a standalone part of Palanteer and used to manage generic todo lists.
The principle is simple:
 - search inside all provided files for a marker (namely "%s" followed by the main category) and
   collect text up to the end of the line.
 - filter, reorder the list of the collected items and display them

An item can contain the following "attributes":
 - P<float>     : priority (lower is more urgent. No priority means priority 99.)
 - W<float>     : the workload in man-day
 - [<category>] : defines an additional category <category>
Example of detected line:    a = a+1; // @REWORK P2 W0.5  [EXAMPLE] [OPTIM] This part is suboptimal

Syntax: %s [options] <filenames>+

Options are:
  Filtering:
  -p  <min> <max>  keep only items within the priority range
  -c  <category>   keep only this category (may be used multiple times, with OR behavior)
  -nc <category>   exclude any item containing this category (may be used multiple times, with OR behavior)
  -sp              sort per priority (default)
  -sf              sort per file,     then priority
  -sc              sort per category, then priority. Duplicate items with multiple categories
  Display:
  -m                   monocolor (default: use ANSI color escape)
  -f <none|base|full>  kind of display of the filenames (none, just the basename (default), or full path)

Try it! (fake todos are contained inside the tool file)
  %s %s
  %s %s -c OPTIM -c BUG
  %s %s -sc
  %s %s -p 0 1 -f none
"""

# Some examples for showcase
# Just call todo.py on itself (./todo.py todo.py)
"""
Base task for fixing a bug @#BUG [EXAMPLE] The behavior is not correct
Task with a priority   @#OPTIM [EXAMPLE] P4 This for loop can be rewritten more efficiencly
Task with a workload   @#DOC [EXAMPLE] W1.5 Do not forget to update the doc for this new function doManyThings()
Task with additional categories   @#OPTIM [EXAMPLE][URGENT] P3 We might consider optimizing this
Multiline task @#DOC There are many things to say about this task
//                   That is why several lines are required P4 [ROGNTUDJU] [EXAMPLE]
#                    What matters is the start column of the text compared to the marker (comment excluded)
Task with all  @#OPTIM P5 [EXAMPLE] [REWORK] W0.5 [EASY] Tabulate the linearly interpolated cosinus values

"""

# Main entry
# ==========


def main(argv):

    # Command line parameters parsing
    # ===============================
    doUseColor = True
    sortKind = "priority"
    fileDisplayKind = "base"
    oneLineMode = False
    doPrintUsage = False
    filterInCategory = []
    filterOutCategory = []
    filenames = []
    prioMin, prioMax = None, None

    i = 1
    while i < len(argv):
        if argv[i].lower() in ["-f", "/f"]:
            if i + 1 >= len(argv):
                print("Error: -f requires a parameter", file=sys.stderr)
                doPrintUsage = True
            else:
                fileDisplayKind = argv[i + 1].lower()
                if fileDisplayKind not in ["none", "base", "full"]:
                    print(
                        "Error: -f requires a value among [none, base, full]",
                        file=sys.stderr,
                    )
                    doPrintUsage = True
                i = i + 1
        elif argv[i].lower() in ["-sc", "/sc"]:
            sortKind = "categories"
        elif argv[i].lower() in ["-sf", "/sf"]:
            sortKind = "file"
        elif argv[i].lower() in ["-sp", "/sp"]:
            sortKind = "priority"
        elif argv[i].lower() in ["-m", "/m"]:
            doUseColor = False
        elif argv[i].lower() in ["-c", "/c"]:
            if i + 1 >= len(argv):
                print("Error: -c requires a parameter", file=sys.stderr)
                doPrintUsage = True
            else:
                filterInCategory.append(argv[i + 1].upper())
                i = i + 1
        elif argv[i].lower() in ["-nc", "/nc"]:
            if i + 1 >= len(argv):
                print("Error: -nc requires a parameter", file=sys.stderr)
                doPrintUsage = True
            else:
                filterOutCategory.append(argv[i + 1].upper())
                i = i + 1
        elif argv[i].lower() in ["-p", "/p"]:
            if i + 2 >= len(argv):
                print("Error: -p requires two parameters", file=sys.stderr)
                doPrintUsage = True
            else:
                try:
                    prioMin, prioMax = float(argv[i + 1]), float(argv[i + 2])
                    if prioMin > prioMax:
                        doPrintUsage = True
                except:
                    print("Error: -p requires two numeric parameters", file=sys.stderr)
                    doPrintUsage = True
                i = i + 2
        elif argv[i][0] == "-":
            doPrintUsage = True  # Unknown option
            print("Unknown option '%s'" % argv[i], file=sys.stderr)
        else:
            filenames.append(argv[i])
        i = i + 1

    if doUseColor:
        RED, GREEN, CYAN, YELLOW, DWHITE, NORMAL = (
            "\033[91m",
            "\033[92m",
            "\033[96m",
            "\033[93m",
            "\033[37m",
            "\033[0m",
        )
    else:
        RED, GREEN, CYAN, YELLOW, DWHITE, NORMAL = "", "", "", "", "", ""
    if sortKind == "file" and fileDisplayKind == "none":
        fileDisplayKind = "base"  # Sanity against weird configuration
    if not filenames:
        doPrintUsage = True

    if doPrintUsage:
        print(
            USAGE
            % (
                MARKER,
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
            ),
            file=sys.stderr,
        )
        sys.exit(1)

    # Todo items collection
    # =====================
    todos = []
    for f in filenames:
        # Load the file
        lines = []
        try:
            with open(f, "r") as fHandle:
                lines = fHandle.readlines()
        except:
            print(" Warning: unable to read file %s" % f)

        # Loop on lines
        lineNbr, lineQty = 0, len(lines)
        while lineNbr < lineQty:

            # Detect the marker
            l = lines[lineNbr]
            m = MATCH_MARKER.match(l)
            if not m:
                lineNbr += 1
                continue

            itemLineNbr = lineNbr + 1  # Lines in a file are counted from 1
            filename = f
            if fileDisplayKind == "full":
                filename = f
            elif fileDisplayKind == "none":
                filename = ""
            else:
                filename = os.path.basename(f)

            # Get the full description of the item, including next lines whose indentation is strictly bigger than the todo item
            startCol = len(m.group(1))  # Required to detect multiline
            descr = [m.group(4).strip()]
            while (
                lineNbr + 1 < lineQty
                and MARKER not in lines[lineNbr + 1]
                and not lines[lineNbr + 1][: startCol + 1]
                .replace("//", "")
                .replace(" ", "")
                .replace("#", "")
                and lines[lineNbr + 1][startCol + 1 :].strip()
            ):
                # Include the next line in the description
                descr.append(lines[lineNbr + 1][startCol + 1 :].strip())
                lineNbr += 1
            lineNbr += 1

            # Extract the attributes
            priority, workload, categories = 99.0, 0.0, [m.group(2)]
            for dLineNbr in range(len(descr)):
                m = MATCH_PRIORITY.match(descr[dLineNbr])
                while m:
                    try:
                        priority = float(m.group(3).strip())
                    except:
                        pass
                    descr[dLineNbr] = (
                        m.group(1) + (" " if m.group(1) else "") + m.group(5)
                    )
                    m = MATCH_PRIORITY.match(descr[dLineNbr])
                m = MATCH_WORKLOAD.match(descr[dLineNbr])
                while m:
                    try:
                        workload = float(m.group(3).strip())
                    except:
                        pass
                    descr[dLineNbr] = (
                        m.group(1) + (" " if m.group(1) else "") + m.group(5)
                    )
                    m = MATCH_WORKLOAD.match(descr[dLineNbr])
                m = MATCH_CATEGORY.match(descr[dLineNbr])
                while m:
                    categories.append(m.group(2).strip())
                    descr[dLineNbr] = (
                        m.group(1) + (" " if m.group(1) else "") + m.group(3)
                    )
                    m = MATCH_CATEGORY.match(descr[dLineNbr])

            # Store the todo item (as a dictionaries, which are easily extensible and easy to read)
            todos.append(
                {
                    "descr": descr,
                    "file": filename,
                    "lineNbr": itemLineNbr,
                    "priority": priority,
                    "workload": workload,
                    "categories": categories,
                }
            )

    # Re-organize items
    # =================
    # Filtering on priorities
    if prioMin != None and prioMax != None:
        filteredTodos = []
        for todo in todos:
            if todo["priority"] >= prioMin and todo["priority"] <= prioMax:
                filteredTodos.append(todo)
        todos = filteredTodos

    # Filtering on categories
    if filterInCategory:
        # In: only the listed category are kept
        filteredTodos = []
        for todo in todos:
            if [1 for c in todo["categories"] if c.upper() in filterInCategory]:
                filteredTodos.append(todo)
        todos = filteredTodos
    if filterOutCategory:
        # Out: only items not containing any of these categories are kept
        filteredTodos = []
        for todo in todos:
            if not [1 for c in todo["categories"] if c.upper() in filterOutCategory]:
                filteredTodos.append(todo)
        todos = filteredTodos

    # Sort by priority
    todos.sort(key=lambda x: x["priority"])

    # Create the display groups
    groups = []
    if sortKind == "categories":
        # Create the groups
        allCategories = sorted(
            list(set(sum([x["categories"] for x in todos], []))),
            key=lambda x: x.lower(),
        )  # "sum(list, [])" = trick to merge several lists
        if filterInCategory:
            allCategories = [c for c in allCategories if c in filterInCategory]
        for c in allCategories:
            groups.append(
                (c.upper(), RED, [t for t in todos if c.upper() in t["categories"]])
            )
        noCategoryTodos = [t for t in todos if not t["categories"]]
        if noCategoryTodos:
            groups.append(("(No category)", RED, noCategoryTodos))
    elif sortKind == "file":
        allFiles = sorted(
            list(set([x["file"] for x in todos])), key=lambda x: x.lower()
        )
        for f in allFiles:
            groups.append((f, GREEN, [t for t in todos if t["file"] == f]))
    else:
        allIntPriorities = sorted(list(set([int(x["priority"]) for x in todos])))
        for p in allIntPriorities:
            groups.append(("", DWHITE, [t for t in todos if int(t["priority"]) == p]))

    # Display items
    # =============

    # Compute widths
    maxWidthFilename = 6 + max([0] + [len(t["file"]) for t in todos])
    maxWidthCategory = max(
        [0] + [sum([1 + len(c) for c in t["categories"]]) for t in todos]
    )
    descrIndent = (
        (6 + 2)
        + (maxWidthFilename + 2 if sortKind != "file" else 7)
        + (maxWidthCategory + 2 if sortKind != "categories" else 0)
    )

    # Loop on groups
    for groupName, groupColor, groupTodos in groups:
        # Header
        groupWl = sum([0.0] + [t["workload"] for t in groupTodos])
        wlStr = ", %.1f md" % groupWl if groupWl > 0.0 else ""
        headerStr = "%s%s(%d todo%s%s)" % (
            groupName,
            " " if groupName else "",
            len(groupTodos),
            "s" if len(groupTodos) > 1 else "",
            wlStr,
        )
        print("%s%s" % (groupColor, headerStr))
        # List the todos
        for t in groupTodos:
            # Priority
            pStr = ("P%.2f" % t["priority"]) if t["priority"] < 99.0 else "---"
            while pStr[-1] == "0":
                pStr = pStr[:-1]
            if pStr[-1] == ".":
                pStr = pStr[:-1]
            print("%s%-6s  " % (CYAN, pStr), end="")
            prioWidth = len(pStr) - 1
            # Categories
            if sortKind != "categories":
                cStr = " ".join(t["categories"])
                print(
                    "%s%s%s  "
                    % (RED, cStr, " " * max(0, maxWidthCategory - len(cStr))),
                    end="",
                )
            # Filename
            if sortKind != "file":
                if fileDisplayKind != "none":
                    fStr = "%s(%d)" % (t["file"], t["lineNbr"])
                    print(
                        "%s%s%s  "
                        % (GREEN, fStr, " " * max(0, maxWidthFilename - len(fStr))),
                        end="",
                    )
            else:
                fStr = "(L%d)" % t["lineNbr"]
                print("%s%-7s" % (GREEN, fStr), end="")
            # Workload
            wl = t["workload"]
            if wl > 0.0:
                print(
                    "%s(%s)"
                    % (
                        YELLOW,
                        ("%d md" % wl) if float(int(wl)) == wl else "%.1f md" % wl,
                    ),
                    end="",
                )
            # Todo description
            print(NORMAL, end="")
            for i in range(len(t["descr"])):
                if i > 0:
                    print(
                        "%s%s|%s" % (CYAN, " " * prioWidth, NORMAL)
                        + (" " * (descrIndent + 2 - prioWidth))
                        + "%s" % DWHITE,
                        end="",
                    )
                print("%s" % t["descr"][i].lstrip())
        print()

    # Display total statistics
    totalWorkload = sum([0.0] + [t["workload"] for t in todos])
    wlStr = ", %.1f md declared" % totalWorkload if totalWorkload > 0.0 else ""
    print(
        "Total: %d unique todo%s%s" % (len(todos), "s" if len(todos) > 1 else "", wlStr)
    )


# Bootstrap
if __name__ == "__main__":
    main(sys.argv)
