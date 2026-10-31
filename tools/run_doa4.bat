@echo off
rem Starts Dead or Alive 4. build.bat copies this file next to dead_or_alive_4.exe.
rem GAME     = the folder with the extracted game files (the one holding default.xex)
rem USERDATA = where saves and the shader cache are kept
rem The defaults match the folder layout in README.md (C:\doa4\xex\DOA4 and C:\doa4\userdata).
rem If your folders are elsewhere, put the full paths here instead.
set "GAME=%~dp0..\..\..\..\xex\DOA4"
set "USERDATA=%~dp0..\..\..\..\userdata"
for %%I in ("%GAME%") do set "GAME=%%~fI"
for %%I in ("%USERDATA%") do set "USERDATA=%%~fI"
if not exist "%GAME%\default.xex" goto no_game
if not exist "%USERDATA%" mkdir "%USERDATA%"
cd /d "%~dp0"
start "" dead_or_alive_4.exe --game_data_root="%GAME%" --user_data_root="%USERDATA%"
exit /b 0

:no_game
echo Could not find default.xex in "%GAME%".
echo Open this file in Notepad and set GAME to your extracted DOA4 folder (see README.md).
pause
