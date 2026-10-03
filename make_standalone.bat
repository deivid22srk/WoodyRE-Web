@echo off
rem make_standalone.bat - one exe with everything inside (WoodyRE-standalone.exe): WoodyRE.exe + YOUR copy of the game files
rem from data\. It unpacks itself to %LOCALAPPDATA%\WoodyRE at its first start. Because it contains the game's data it is for
rem your own use only: never share or upload it. Start WoodyRE.exe once before, so data\ exists.
call "%~dp0build.bat" standalone
if errorlevel 1 pause
