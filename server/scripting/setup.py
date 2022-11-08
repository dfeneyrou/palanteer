# Palanteer scripting library
# Copyright (C) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import io
import os
import os.path
import sys
import glob
import shutil
from setuptools import setup, find_packages, Extension

destC = "palanteer_scripting/_cextension"

# If in-source, copy required C++ sources inside the folder (constraint from setup.py)
# They will be removed after the packaging
if os.path.isfile("../../c++/palanteer.h"):
    shutil.copyfile("../../c++/palanteer.h", destC + "/palanteer.h")
    shutil.copytree("../external/zstd", destC + "/zstd")
    shutil.copytree("../common", destC + "/common")
    os.mkdir(destC + "/base")
    for f in ["bsString.cpp", "bsOsLinux.cpp", "bsOsWindows.cpp"]:
        shutil.copyfile("../base/%s" % f, destC + "/base/%s" % f)
    for h in glob.glob("../base/*.h"):
        print(os.path.basename(h))
        shutil.copyfile(h, destC + "/base/%s" % os.path.basename(h))

# Get the source file list
src_list = (
    glob.glob(destC + "/base/*.cpp")
    + glob.glob(destC + "/common/*.cpp")
    + glob.glob(destC + "/zstd//*/*.c")
    + glob.glob(destC + "/*.cpp")
)
src_list = [
    os.path.normpath(s) for s in src_list
]  # Normalize all source path (required for Windows)


extra_link_args = []
extra_compilation_flags = [
    "-DUSE_PL=1",
    "-DPL_EXPORT=1",
    "-DBS_NO_GRAPHIC",
    "-DPL_NOCONTROL=1",
    "-DPL_NOEVENT=1",
    "-DPL_GROUP_BSVEC=0",  # Force deactivation of array bound check
    "-std=c++14",
]

extra_compilation_flags.extend(["-I", os.path.normpath(destC)])
for folder in [
    "base",
    "common",
    "zstd",
    "zstd/common",
    "zstd/compress",
    "zstd/decompress",
]:
    extra_compilation_flags.extend(["-I", os.path.normpath(destC + "/%s" % folder)])

if sys.platform == "win32":
    extra_compilation_flags.append("/DUNICODE")

classifiers_list = [
    "Intended Audience :: Developers",
    "Intended Audience :: Science/Research",
    "License :: OSI Approved :: GNU Affero General Public License v3 or later (AGPLv3+)",
    "Development Status :: 4 - Beta",
    "Programming Language :: Python :: 3",
    "Programming Language :: Python :: 3.7",
    "Programming Language :: Python :: 3.8",
    "Programming Language :: Python :: 3.9",
    "Programming Language :: Python :: 3.10",
    "Programming Language :: Python :: Implementation :: CPython",
    "Operating System :: Microsoft :: Windows :: Windows 10",
    "Operating System :: POSIX :: Linux",
    "Topic :: Software Development",
    "Topic :: Software Development :: Testing",
]

# Read the Palanteer version from the C++ header library
with io.open(destC + "/palanteer.h", encoding="UTF-8") as versionFile:
    PALANTEER_VERSION = (
        [l for l in versionFile.read().split("\n") if "PALANTEER_VERSION " in l][0]
        .split()[2]
        .replace('"', "")
    )

# Read the content of the readme file
with io.open("README.md", encoding="UTF-8") as readmeFile:
    long_description = readmeFile.read()


# Build call
setup(
    name="palanteer_scripting",
    version=PALANTEER_VERSION,
    author="Damien Feneyrou",
    author_email="dfeneyrou@gmail.com",
    license="AGPLv3+",
    description="Palanteer scripting module",
    long_description=long_description,
    long_description_content_type="text/markdown",
    classifiers=classifiers_list,
    python_requires=">=3.7",
    url="https://github.com/dfeneyrou/palanteer",
    packages=find_packages(),
    ext_modules=[
        Extension(
            "palanteer_scripting._cextension",
            sources=src_list,
            extra_compile_args=extra_compilation_flags,
            extra_link_args=extra_link_args,
        ),
    ],
    py_modules=["palanteer_scripting._scripting"],
    zip_safe=False,
)


# If in-source, remove the temporarily copied sources (cleanup)
if os.path.isfile("../../c++/palanteer.h"):
    os.unlink(destC + "/palanteer.h")
    for folder in ["base", "common", "zstd"]:
        shutil.rmtree(destC + "/%s" % folder)
