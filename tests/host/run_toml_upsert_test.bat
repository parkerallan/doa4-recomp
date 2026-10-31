@echo off
rem Builds and runs the TOML upsert host test with clang-cl (no CMake).
rem toml++ comes from the ReXGlue SDK checkout beside this repo.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul 2>&1
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
set "OUT=%TEMP%\doa4_toml_upsert_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 -I "%~dp0..\..\..\rexglue-sdk\thirdparty\tomlplusplus\include" "%~dp0toml_upsert_host_test.cpp" "%~dp0..\..\src\doa4_config.cpp" /Fo"%OUT%\\" /Fe"%OUT%\toml_upsert_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\toml_upsert_host_test.exe"
