@echo off
REM ===========================================================================
REM  Devil's Roulette - one-click build script
REM  Requires MinGW-w64 g++ (this machine: C:\mingw64\bin\g++.exe)
REM
REM  IMPORTANT: -fexec-charset=GBK is required. Modern GCC encodes narrow
REM  string literals as UTF-8 by default, but this game must print GBK(CP936)
REM  text, otherwise Chinese is garbled on a CP936 Windows console.
REM
REM  This file is kept PURE ASCII on purpose: cmd.exe parses .bat files in the
REM  OEM codepage, and non-ASCII bytes in it can be mis-parsed. The Chinese
REM  output names are written with the "for" trick below.
REM ===========================================================================
setlocal
set GPP=C:\mingw64\bin\g++.exe
if not exist "%GPP%" set GPP=g++

set CXXFLAGS=-std=c++17 -O2 -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=GBK

if not exist build mkdir build

echo [1/2] Building OPTIMIZED + FIXED version ...
"%GPP%" %CXXFLAGS% -o "build\DevilRoulette_optimized.exe" "optimized.cpp"
if errorlevel 1 goto fail

echo [2/2] Building FAITHFUL reconstruction (keeps the original bugs) ...
"%GPP%" %CXXFLAGS% -o "build\DevilRoulette_faithful.exe" "faithful.cpp"
if errorlevel 1 goto fail

echo.
echo Build OK. Output in .\build\
echo   DevilRoulette_optimized.exe  (recommended: bugs fixed + optimized)
echo   DevilRoulette_faithful.exe   (faithful: still has the original bugs)
exit /b 0

:fail
echo.
echo BUILD FAILED
exit /b 1
