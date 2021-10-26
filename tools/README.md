This folder contains various standalone tools, all in Python.

Content
=======

**extStringCppParser.py** <br/>
This tool is associated to the "external string" feature of the C++ instrumentation library. <br/>
It parses (with simple regexp) the provided files, extracts and hashes the static strings used in the instrumentation API
and also the filenames, and output them in a "lookup" file that the server understands.

**extStringDecoder.py** <br/>
This tool is associated to the "external string" feature of the C++ instrumentation library. <br/>
It simply replaces the hashes in the input stream thanks to the provided lookup file. <br/>
Its typical usage is to "decode" assertions from the console, when using the "external string" feature.

**palanteer_stub.py** <br/>
This Python module implements the Python instrumentation API with empty functions. <br/>
Its only usage is to use it as a fallback when distributing an instrumented script so that it can run without
requiring the installation of Palanteer.

**testframework.py** <br/>
This tool is the simple standalone test framework used internally, taking advantage of the Palanteer stimulation and observation capabilities. <br/>
Coupled with the test scripts, they also form an example of how to test with Palanteer.

**todo.py** <br/>
This standalone tool is the implementation of the "inline todo list" discussed in the section ["more"](https://dfeneyrou.github.io/palanteer/more.md.html#more/thoughtsonsoftwarequality/awaytomanagetodos) <br/>
It is also used in the making of this project.
