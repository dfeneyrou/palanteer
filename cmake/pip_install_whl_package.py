import os
import sys
import subprocess
import glob

# This file exists because there is no easy way to have a portable wildcard
#  in file name in CMake for generated files

python_path = sys.argv[1]
whl_folder_path = sys.argv[2]

whl_path = glob.glob("%s/dist/*.whl" % whl_folder_path)[0]

print("Installing %s" % whl_path)
subprocess.run(
    [python_path, "-m", "pip", "install", whl_path, "--force-reinstall"],
    universal_newlines=True,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
