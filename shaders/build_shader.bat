@echo off
CALL  python compileShaders.py --dxc ..\libs\dxc\bin\x64\dxc.exe
IF %ERRORLEVEL% NEQ 0 (
  PAUSE
)