@echo off
rem Windows build of the GPU Boruvka and of its benchmark. Run from the project root:
rem   cuda\build.bat            builds build\boruvka_cuda.dll and bin\bench_mst.exe
rem   set ARCH=sm_86 & cuda\build.bat      for another GPU (default sm_50)
rem
rem Needs the CUDA toolkit, Visual Studio (nvcc uses cl.exe as host compiler; CUDA 11.4
rem supports up to VS 2019, edit VCVARS below if yours is elsewhere) and a 64 bit gcc
rem on the PATH (MinGW-w64, e.g. WinLibs): a 32 bit one cannot load the 64 bit DLL.
setlocal
if not defined ARCH set ARCH=sm_50
if not defined VCVARS set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat

for /f %%m in ('gcc -dumpmachine') do set GCC_TARGET=%%m
echo %GCC_TARGET% | findstr /b x86_64 >nul || (echo gcc on the PATH is %GCC_TARGET%, a 64 bit one ^(x86_64^) is required & exit /b 1)

if not exist build mkdir build
if not exist bin mkdir bin

call "%VCVARS%" >nul || exit /b 1
nvcc -O3 -arch=%ARCH% -Wno-deprecated-gpu-targets -shared -o build\boruvka_cuda.dll cuda\boruvka.cu || exit /b 1

gcc -std=gnu11 -O2 -Iinclude cuda/bench_mst.c src/graph.c src/list.c src/star_impl.c src/matrix_impl.c src/queue.c src/stack.c src/importer.c src/traversal.c src/export.c -lm -o bin/bench_mst.exe || exit /b 1
echo built build\boruvka_cuda.dll and bin\bench_mst.exe
