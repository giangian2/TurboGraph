@echo off
rem Builds build\boruvka_cli.exe with nvcc 11.4 + MSVC 2019 (the newest host compiler 11.4 supports).
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build mkdir build
nvcc -O3 -arch=sm_50 -Wno-deprecated-gpu-targets -o build\boruvka_cli.exe cuda\boruvka_cli.cu
