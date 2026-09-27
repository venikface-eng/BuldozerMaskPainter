@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   Buldozer Landscape Mask Painter - Build Script
echo ========================================================

:: Find Visual Studio installation path
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo [ERROR] Visual Studio installation not found!
    pause
    exit /b 1
)

echo [INFO] Found Visual Studio at: %VS_PATH%

:: Call vcvars64.bat
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to initialize x64 MSVC environment.
    pause
    exit /b 1
)

:: Find CMake
set "CMAKE_BIN=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_BIN%" (
    set "CMAKE_BIN=cmake"
)

echo [INFO] Using CMake: %CMAKE_BIN%

:: Create build folder
if not exist "build" mkdir "build"
cd build

:: If CMakeCache exists with different generator, clean it
if exist "CMakeCache.txt" (
    findstr /C:"Visual Studio 17 2022" CMakeCache.txt >nul
    if %errorlevel% neq 0 (
        del /f /q CMakeCache.txt >nul 2>&1
    )
)

:: Configure CMake
echo [INFO] Configuring CMake project...
"%CMAKE_BIN%" .. -A x64
if %errorlevel% neq 0 (
    echo [INFO] Retrying with clean cache...
    del /f /q CMakeCache.txt >nul 2>&1
    "%CMAKE_BIN%" .. -A x64
)
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    if "%1"=="" pause
    exit /b 1
)

:: Build Release x64
echo [INFO] Building BuldozerMaskPainter.dll (Release x64)...
"%CMAKE_BIN%" --build . --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    if "%1"=="" pause
    exit /b 1
)

echo ========================================================
echo [SUCCESS] BuldozerMaskPainter.dll successfully compiled!
echo Location: %~dp0bin\Release\BuldozerMaskPainter.dll
echo ========================================================

cd ..
if "%1"=="" pause
