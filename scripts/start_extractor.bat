@echo off
echo ===================================
echo ArcadeMatrix MUGEN / Sprite Extractor
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
echo Please enter the path to the folder containing your source GIFs:
set /p input_folder="Input Folder: "

echo.
echo Please enter the path where you want the .fgt files saved:
set /p output_folder="Output Folder (e.g. ./output/ryu): "

echo.
python mugen_extractor.py -i "%input_folder%" -o "%output_folder%"

echo.
echo [DONE]
pause
