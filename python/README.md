This folder contains the Python instrumentation module.

Installation
============

Requires that `setuptools` (comes with pip) and `wheel` Python packages are installed (standard installers).

A global `Palanteer` installation is described in ./INSTALL.md . <br/>
The CMake target for this component is "python_instrumentation"


Usage
=====

Profiling can be done:

1) With unmodified code:  `python -m palanteer [options] <your script>`

     This syntax is similar to the `cProfile` usage and no script modification is required. <br/>
     By default, it tries to connect to a Palanteer server. With options, offline profiling can be selected. <br/>
     Launch `python -m palanteer` for help or refer to the [documentation](https://dfeneyrou.github.io/palanteer/instrumentation_api_python.md.html#pythoninstrumentationapi/initialization/automaticinstrumentationwithoutcodemodification).

2) With code instrumentation:

    Please refer to the [documentation](https://dfeneyrou.github.io/palanteer/instrumentation_api_python.md.html) for details. <br/>
    Manual instrumentation can provide additional valuable information compared to just the automatic function profiling, like data, locks, ...

