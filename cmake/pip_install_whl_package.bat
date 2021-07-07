@echo off

rem Find the WHL file (whose name can have multiple forms depending on the python version and platform)
set whlFilename=
for /F "delims=*" %%G in ('dir /b %2\dist\*.whl') do set "whlFilename=%2/dist/%%G"

rem Install the Wheel package
echo Calling:  %1 -m pip install %whlFilename%
%1 -m pip install %whlFilename%
