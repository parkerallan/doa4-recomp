@echo off
rem Builds and runs the game files host test with clang-cl (no CMake).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul 2>&1
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
set "OUT=%TEMP%\doa4_gamefiles_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 "%~dp0gamefiles_host_test.cpp" "%~dp0..\..\src\doa4_gamefiles.cpp" "%~dp0..\..\src\doa4_xdvdfs.cpp" /Fo"%OUT%\\" /Fe"%OUT%\gamefiles_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\gamefiles_host_test.exe"
