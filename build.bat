@echo off
rem Builds the Dead or Alive 4 recomp. Double-click it, or run it from a command window.
rem It finds Visual Studio's C++ tools and LLVM, fixes the SDK's symlink files, then
rem configures and builds. The ReXGlue SDK is expected next to this folder (..\rexglue-sdk);
rem set REXSDK_DIR first to use another place.
rem No parenthesised blocks: a path such as "Program Files (x86)" would end them early.
setlocal
cd /d "%~dp0"

if not defined REXSDK_DIR set "REXSDK_DIR=%~dp0..\rexglue-sdk"
for %%I in ("%REXSDK_DIR%") do set "REXSDK_DIR=%%~fI"
if not exist "%REXSDK_DIR%\CMakeLists.txt" goto no_sdk

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_vs
set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL goto no_vs
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
cd /d "%~dp0"

if exist "%ProgramFiles%\LLVM\bin\clang.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
where clang >nul 2>nul
if errorlevel 1 goto no_llvm

set "PY=py -3"
where py >nul 2>nul
if errorlevel 1 set "PY=python"
%PY% --version >nul 2>nul
if errorlevel 1 goto no_python

echo.
echo === Fixing the SDK's symlink files (safe to repeat)
%PY% tools\fix_git_symlinks.py "%REXSDK_DIR%"
if errorlevel 1 goto failed

set "SDKFWD=%REXSDK_DIR:\=/%"
echo.
echo === Configuring (SDK: %REXSDK_DIR%)
cmake --preset win-amd64-release "-DREXSDK_DIR=%SDKFWD%" "-DCMAKE_C_FLAGS=-march=x86-64-v3" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3"
if errorlevel 1 goto failed

echo.
echo === Building (the first build takes a long time; later builds are quick)
cmake --build --preset win-amd64-release
if errorlevel 1 goto failed

if not exist "out\build\win-amd64-release\run_doa4.bat" copy /y "tools\run_doa4.bat" "out\build\win-amd64-release\" >nul
echo.
echo === Done. To play, open out\build\win-amd64-release and double-click run_doa4.bat
goto end

:no_sdk
echo Could not find the ReXGlue SDK at "%REXSDK_DIR%".
echo Download it next to this folder as described in README.md (step 3).
goto end
:no_vs
echo Could not find Visual Studio's C++ build tools.
echo Install "Build Tools for Visual Studio" with "Desktop development with C++" (README.md, step 1).
goto end
:no_llvm
echo Could not find LLVM (clang). Install LLVM as described in README.md (step 1).
goto end
:no_python
echo Could not find Python. Install Python 3 as described in README.md (step 1).
goto end
:failed
echo.
echo The build stopped with an error (see the messages above and README.md, "If something goes wrong").
:end
echo.
pause
