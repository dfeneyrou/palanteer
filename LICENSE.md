# License

`Palanteer` uses two different licenses, depending on the components and their inherent constraints:
  1. Instrumentation libraries shall preserve the user's freedom to distribute their program in closed sources
     - In this case, the **MIT license** applies
  1. Improvement on the server tooling side shall benefit to the community
     - In this case, the **Affero GNU General Public License version 3 or later (AGPL v3+)** applies


The big lines are:

  - `./c++`, `./python` and `./tools` are under the **MIT license**
    - these folders contain the instrumentation libraries and helper tools
    - This permissive license preserves developers's rights about distributing their software, even if delivered with instrumentation (modified or not).
  - `./server/base` is also under the **MIT license**
    - as an exception for server side, the code in this folder, if useful, can be reused in closed source projects.
  - `./server/common`, `./server/viewer` and `./python/python_scripting` are under the **AGPL v3+ license**
    - these parts shall benefit to the community (i.e. sources must be shared if a derivative is distributed) while free to use and modify
    - "Affero" version of the GPL was naturally chosen to cover also the case of distribution over network

To remove any ambiguity, each folder contains the associated license and each file has a license header.

The snapshotted external dependencies have the following licenses:

| Dependency name                  | License type                | URL                                            | Used by           | Location in the project      |
| ---------                        | -----------                 | ----                                           | ---               | --------                     |
| Khronos OpenGL API and Extension | MIT                         | https://www.khronos.org/registry/OpenGL/api/GL | Viewer            | server/external/             |
| Dear ImGui                       | MIT                         | https://github.com/ocornut/imgui               | Viewer            | server/external/imgui        |
| stb_image                        | Public domain               | https://github.com/nothings/stb                | Viewer            | server/external/stb_image.h  |
| Fonts 'Roboto-Medium.ttf'        | Apache License, Version 2.0 | https://fonts.google.com/specimen/Roboto       | Viewer            | server/viewer/vwFontData.cpp |
| ZStandard                        | BSD                         | https://facebook.github.io/zstd                | Viewer, scripting | server/external/zstd         |
| Markdeep                         | BSD                         | https://casual-effects.com/markdeep            | Documentation     | doc/                         |
