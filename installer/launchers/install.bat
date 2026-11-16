@echo off
rem GD Compare setup for Windows: double-click this file.
setlocal
cd /d "%~dp0"
if not exist "bin\gdcompare-setup.exe" (
  echo bin\gdcompare-setup.exe is missing. Unzip the whole GDCompare zip, or restore the file if your antivirus removed it.
  pause
  exit /b 1
)
"bin\gdcompare-setup.exe" %*
set status=%errorlevel%
echo.
pause
exit /b %status%
