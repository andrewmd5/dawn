@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" arm64
cd /d C:\Users\andrew\projects\dawn
cmake --build build
