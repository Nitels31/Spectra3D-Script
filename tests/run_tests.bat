@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."

if not exist bin\spsc.exe (
    echo [tests] bin\spsc.exe not found - run build.bat first
    exit /b 1
)

set FAILED=0
set COUNT=0

for %%F in (tests\*.sps) do (
    set "NAME=%%~nF"
    set "ACTUAL=%TEMP%\sps_test_%%~nF.txt"

    set /a COUNT+=1
    "bin\spsc.exe" "%%F" --run > "!ACTUAL!" 2>&1

    fc /b "tests\%%~nF.out" "!ACTUAL!" >nul 2>&1
    if errorlevel 1 (
        echo [FAIL] !NAME!
        type "!ACTUAL!"
        set /a FAILED+=1
    ) else (
        echo [ OK ] !NAME!
    )
)

rem the api test: a host application that embeds the language
if exist bin\host_example.exe (
    set /a COUNT+=1
    "bin\host_example.exe" examples\host\game.sps > "%TEMP%\sps_test_api.txt" 2>&1
    fc /b examples\host\expected.txt "%TEMP%\sps_test_api.txt" >nul 2>&1
    if errorlevel 1 (
        echo [FAIL] api_host
        type "%TEMP%\sps_test_api.txt"
        set /a FAILED+=1
    ) else (
        echo [ OK ] api_host
    )
) else (
    echo [SKIP] api_host - run examples\host\build_host.bat
)

rem the spl test: the C library and the C++ header must agree
if exist bin\spl_compat_test.exe (
    set /a COUNT+=1
    "bin\spl_compat_test.exe" assets\level_01.spl > "%TEMP%\sps_test_spl.txt" 2>&1
    fc /b tests\spl_expected.txt "%TEMP%\sps_test_spl.txt" >nul 2>&1
    if errorlevel 1 (
        echo [FAIL] spl_compat
        type "%TEMP%\sps_test_spl.txt"
        set /a FAILED+=1
    ) else (
        echo [ OK ] spl_compat
    )
) else (
    echo [SKIP] spl_compat - run build.bat
)

echo.
if !FAILED! gtr 0 (
    echo [tests] !FAILED! of !COUNT! tests failed
    exit /b 1
)
echo [tests] all !COUNT! tests passed
exit /b 0
