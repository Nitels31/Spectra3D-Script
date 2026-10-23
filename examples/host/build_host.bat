@echo off
setlocal enabledelayedexpansion
set "CC=C:/msys64/ucrt64/bin/g++.exe"
set "MSYS_BIN=C:\msys64\ucrt64\bin"
set "PATH=%MSYS_BIN%;%PATH%"

if not exist bin mkdir bin

echo [build] bin/host_example.exe
"%CC%" -std=c++17 -O2 -Wall -Wextra -Iinclude examples\host\host_example.cpp bin\libsps.a -o bin\host_example.exe
if errorlevel 1 exit /b 1

echo [build] ok
