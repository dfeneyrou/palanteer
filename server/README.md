This folder contains the server side of Palanteer.

Content
=======

- `base`: folder containing the platform C++ code (OS abstraction, openGL wrapper, STL-like utils...)
- `common`: (AGPLv3+) folder containing the event recording and reading library. Used by both the scripting module and the viewer.
- `viewer`: (AGPLv3+) folder containing the viewer application
- `scripting`: (AGPLv3+) folder containing the Python scripting module, and its C extension
- `external`: folder containing snapshots of library dependencies


Installation
============

Install all `Palanteer` components with CMake (see ./INSTALL.md). <br/>
The individual targets are "viewer" and "scripting".
