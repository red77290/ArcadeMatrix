@echo off
setlocal enabledelayedexpansion
echo ===================================
echo ArcadeMatrix Font Converter (BDF -^> AMF)
echo ===================================

cd /d "%~dp0"

IF NOT EXIST "venv\Scripts\activate.bat" (
    echo [INFO] Creating Python Virtual Environment...
    python -m venv venv
    if errorlevel 1 (
        echo [ERROR] Python is not installed or not in PATH!
        pause
        exit /b 1
    )
)

echo [INFO] Activating virtual environment...
call venv\Scripts\activate.bat

echo [INFO] Installing requirements...
pip install -r requirements.txt -q
if errorlevel 1 (
    echo [ERROR] Failed to install Python dependencies!
    pause
    exit /b 1
)

echo.
echo Please enter the path to your SD card root or fonts folder:
set "sd_path="
set /p sd_path="SD Card Path (e.g. E:\ or E:\fonts): "
set "sd_path=!sd_path:"=!"
if "!sd_path!"=="" (
    echo [ERROR] SD Card Path cannot be empty.
    pause
    exit /b 1
)
if "!sd_path:~-1!"=="\" set "sd_path=!sd_path!."

echo.
python generate_fonts.py "!sd_path!"

echo.
echo [DONE]
pause
