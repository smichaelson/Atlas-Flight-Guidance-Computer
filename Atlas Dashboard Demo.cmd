@echo off
"%~dp0.venv\Scripts\python.exe" -B "%~dp0tools\bringup\launch_ground_station.py" --demo
if errorlevel 1 pause
