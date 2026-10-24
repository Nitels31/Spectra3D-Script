@echo off
setlocal
set "CC=C:/msys64/ucrt64/bin/gcc.exe"
set "CXX=C:/msys64/ucrt64/bin/g++.exe"
set "MSYS_BIN=C:\msys64\ucrt64\bin"
set "PATH=%MSYS_BIN%;%PATH%"
set "CFLAGS=-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-clobbered -static -Isrc -Iinclude"
set "CORE=src/arena.c src/object.c src/dict.c src/methods.c src/spl.c src/spslib.c src/lexer.c src/parser.c src/compiler.c src/vm.c src/program.c src/builtins.c src/regex.c src/term.c"
set "API=src/api/api_value.c src/api/api_context.c src/api/api_function.c src/api/api_class.c src/api/api_compiler.c src/api/api_version.c"

if not exist bin mkdir bin
if not exist out mkdir out
del *.o objlist.txt 2>nul

echo [build] bin/spsc.exe
"%CC%" %CFLAGS% %CORE% src/api/api_version.c src/main.c -o bin/spsc.exe
if errorlevel 1 exit /b 1

echo [build] bin/spsvm.exe
"%CC%" %CFLAGS% %CORE% src/vm_main.c -o bin/spsvm.exe
if errorlevel 1 exit /b 1

echo [build] bin/libsps.a
"%CC%" %CFLAGS% -c %CORE% %API%
if errorlevel 1 exit /b 1
dir /b *.o > objlist.txt
"%MSYS_BIN%\ar.exe" rcs bin\libsps.a @objlist.txt
if errorlevel 1 exit /b 1
del *.o objlist.txt

echo [build] bin/spl_compat_test.exe
"%CXX%" -std=c++17 -O2 -Wall -Wextra -Iinclude tests\spl_compat_test.cpp bin\libsps.a -o bin\spl_compat_test.exe
if errorlevel 1 exit /b 1

echo [build] ok
