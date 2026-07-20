@echo off
echo ===================================
echo ArcadeMatrix Font Converter (BDF -^> AMF)
echo ===================================

cd /d "%~dp0"

IF NOT EXIST "venv" (
    echo [INFO] Creating Python Virtual Environment...
    python -m venv venv
    if errorlevel 1 (
        echo [ERROR] Python is not installed or not in PATH!
        pause
        exit /b
    )
)

echo [INFO] Activating virtual environment...
call venv\Scripts\activate.bat

echo [INFO] Installing requirements...
pip install -r requirements.txt -q

echo.
echo Please enter the path to your SD card root or fonts folder:
set /p sd_path="SD Card Path (e.g. E:\ or E:\fonts): "

echo.
python generate_fonts.py "%sd_path%"

echo.
echo [DONE]
pause
