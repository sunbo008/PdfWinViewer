@echo off
setlocal

REM Usage: merge_json.cmd <base.json> <incoming.json> <out.json> <arrayKey>
set "BASE=%~1"
set "INCOMING=%~2"
set "OUT=%~3"
set "KEY=%~4"

if "%BASE%"=="" (
  echo Usage: %~n0 base.json incoming.json out.json [name^|label]
  exit /b 2
)

REM Use built-in Windows PowerShell (no PowerShell 7 required)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0merge_json.ps1" "%BASE%" "%INCOMING%" "%OUT%" "%KEY%"
exit /b %ERRORLEVEL%


