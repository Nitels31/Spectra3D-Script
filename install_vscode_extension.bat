@echo off
setlocal
rem Installs the Spectra3D Script extension into the VS Code extensions folder.
rem Works in cmd.exe (%%USERPROFILE%%). In PowerShell use:
rem   Copy-Item -Recurse .\vscode-spscript "$env:USERPROFILE\.vscode\extensions\spectra3d.spscript-0.1.0"

set "SRC=%~dp0vscode-spscript"
set "DEST=%USERPROFILE%\.vscode\extensions\spectra3d.spscript-0.1.0"

if not exist "%SRC%" (
    echo [install] %SRC% not found
    exit /b 1
)

if not exist "%USERPROFILE%\.vscode\extensions" (
    mkdir "%USERPROFILE%\.vscode\extensions"
)

if exist "%DEST%" (
    echo [install] removing the previous version
    rmdir /s /q "%DEST%"
)

xcopy "%SRC%" "%DEST%" /E /I /Q /Y >nul
if errorlevel 1 (
    echo [install] failed
    exit /b 1
)

echo [install] installed to %DEST%
echo [install] run "Developer: Reload Window" in VS Code to activate it.
exit /b 0
