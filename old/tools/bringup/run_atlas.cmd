@echo off
setlocal DisableDelayedExpansion
set "PYTHONUTF8=1"
if not exist "%~dp0..\..\.venv\Scripts\python.exe" goto find_py
"%~dp0..\..\.venv\Scripts\python.exe" -I -c "import sys; sys.exit(sys.version_info < (3,10))" >nul 2>nul
if errorlevel 1 goto find_py
"%~dp0..\..\.venv\Scripts\python.exe" -B "%~dp0bootstrap.py" %*
set "ATLAS_EXIT=%errorlevel%"
goto finished
:find_py
py -3 -I -c "import sys; sys.exit(sys.version_info < (3,10))" >nul 2>nul
if errorlevel 1 goto find_python
py -3 -B "%~dp0bootstrap.py" %*
set "ATLAS_EXIT=%errorlevel%"
goto finished
:find_python
python -I -c "import sys; sys.exit(sys.version_info < (3,10))" >nul 2>nul
if errorlevel 1 goto missing
python -B "%~dp0bootstrap.py" %*
set "ATLAS_EXIT=%errorlevel%"
goto finished
:missing
echo Atlas needs Python 3.10 or newer on this computer.
echo Install Python from https://www.python.org/downloads/windows/
echo Enable its Python launcher, then run this file again from your clone.
set "ATLAS_EXIT=1"
:finished
if defined ATLAS_NONINTERACTIVE goto return_status
if not "%ATLAS_EXIT%"=="0" pause
if "%~1"=="build" pause
if "%~1"=="servo-build" pause
:return_status
exit /b %ATLAS_EXIT%
