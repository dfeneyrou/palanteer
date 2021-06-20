This folder contains the Python instrumentation module.

Installation
============

Requires that `setuptools` is installed (comes with pip).

You can either:

1) Install all `Palanteer` components with CMake (see ./INSTALL.md) <br/>
   Indeed, this library is part of the global installation (target "python_instrumentation"). <br/>
   The installation in this case is for the current user only, so that no privilege rights are required.

or

2) Run the command below from this folder: <br/>
     `python setup.py install --user`    (for current user only) <br/>
or <br/>
     `python setup.py install`           (for all users, it requires root privilege on Linux) <br/>



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

