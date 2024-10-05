@echo off
cls
CALL:ColorBlue "====== BUILDING ======"
haxe build.hxml
CALL:ColorBlue "====== RUNNING ======"
start "C:\Program Files\HashLink\1.14.0\hl.exe" .\export\hl\main.hl
pause

:ColorBlue
%Windir%\System32\WindowsPowerShell\v1.0\Powershell.exe write-host -foregroundcolor Blue %1
