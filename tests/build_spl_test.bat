@echo off
setlocal
set "CC=C:/msys64/ucrt64/bin/g++.exe"
set "MSYS_BIN=C:\msys64\ucrt64\bin"
set "PATH=%MSYS_BIN%;%PATH%"

if not exist bin mkdir bin

echo [build] bin/spl_compat_test.exe
"%CC%" -std=c++17 -O2 -Wall -Wextra -Iinclude tests\spl_compat_test.cpp bin\libsps.a -o bin\spl_compat_test.exe
if errorlevel 1 exit /b 1

echo [build] ok
