@echo off
title Cleanup: Deleting .vs, x64, bin, and compiled
echo Starting cleanup...

:: Switch to the directory where the script is located
cd /d "%~dp0"

:: 1. Deleting the .vs directory in the root
echo.
echo [1/2] Deleting .vs directory in the root...
if exist ".vs\" (
    rmdir /s /q ".vs"
    echo The .vs directory was deleted.
) else (
    echo The .vs directory was not found.
)

:: 2. Recursively searching for and deleting all x64 directories
echo.
echo [2/2] Searching for and deleting x64 directories...
for /d /r . %%D in (x64) do (
    if exist "%%D\" (
        echo Removing: "%%D"
        rmdir /s /q "%%D"
    )
)

echo.
echo Done!
pause
