@echo off
rem Runs scripts/refresh-emacs.sh in the MSYS2 mingw64 shell, from
rem anywhere that is not one.
rem
rem   scripts\refresh-emacs.cmd

setlocal
set "MSYS2=%USERPROFILE%\scoop\apps\msys2\current"
if not exist "%MSYS2%\usr\bin\bash.exe" set "MSYS2=C:\msys64"
if not exist "%MSYS2%\usr\bin\bash.exe" (
    echo no MSYS2 shell to build in 1>&2
    exit /b 1
)

rem CHERE_INVOKING: the login shell starts where this is run from, which
rem saves turning a Windows path into the shell's own.
cd /d "%~dp0.."
set "MSYSTEM=MINGW64"
set "CHERE_INVOKING=1"
"%MSYS2%\usr\bin\bash.exe" -lc "scripts/refresh-emacs.sh %*"
