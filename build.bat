@echo off
rem build.bat [source] [output] [extra nvcc flags...] - compile a .cu file (default: main_accum.cu -> accum.exe)
set SRC=%1
set OUT=%2
if "%SRC%"=="" set SRC=main_accum.cu
if "%OUT%"=="" set OUT=accum.exe
shift
shift
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
nvcc %SRC% -o %OUT% -arch=sm_89 -std=c++17 %1 %2 %3 %4 %5 %6 %7 %8 %9
if %errorlevel% neq 0 (echo BUILD FAILED & exit /b 1)
echo build OK: %OUT%
