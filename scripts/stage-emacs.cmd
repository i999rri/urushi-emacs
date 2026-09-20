@echo off
rem Runs scripts/stage-emacs.sh in the MSYS2 shell, from anywhere that
rem is not one.
rem
rem   scripts\stage-emacs.cmd

setlocal
set "MSYS2=%USERPROFILE%\scoop\apps\msys2\current"
if not exist "%MSYS2%\usr\bin\bash.exe" set "MSYS2=C:\msys64"
if not exist "%MSYS2%\usr\bin\bash.exe" (
    echo no MSYS2 shell to stage in 1>&2
    exit /b 1
)

cd /d "%~dp0.."
set "MSYSTEM=MINGW64"
set "CHERE_INVOKING=1"
"%MSYS2%\usr\bin\bash.exe" -lc "scripts/stage-emacs.sh %*"
