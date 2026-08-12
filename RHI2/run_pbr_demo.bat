@echo off
setlocal
set "EXE=%~dp0build\bin\pbr_demo.exe"
if not exist "%EXE%" set "EXE=%~dp0build\Release\pbr_demo.exe"
if not exist "%EXE%" (
  echo pbr_demo.exe was not built.
  echo Run: cmake -S . -B build ^&^& cmake --build build --config Release
  pause
  exit /b 1
)
"%EXE%" software
