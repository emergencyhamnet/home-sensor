@echo off
setlocal
REM Installs Python dependencies for the WS-2000 local receiver/dashboard.

python --version >nul 2>&1
if errorlevel 1 (
  echo Python not found. Install Python 3.10+ from https://www.python.org and make sure "Add Python to PATH" is checked.
  pause
  exit /b 1
)

echo Installing requirements...
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
echo Done.
pause
