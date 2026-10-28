@echo off
rem Opens the DOA4 launcher. Without arguments the window starts hidden from this console,
rem so closing the console cannot stop a step half way. With arguments (-DryRun, -Action ...)
rem it runs here and prints to this console. No parenthesised blocks: a folder such as
rem "Program Files (x86)" in %~dp0 would end them early.
if not "%~1"=="" goto args
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0launcher\DOA4Launcher.ps1"
exit /b 0
:args
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launcher\DOA4Launcher.ps1" %*
