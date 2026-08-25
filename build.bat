@echo off
REM build.bat [Debug|Release] [clean]

setlocal enabledelayedexpansion
if exist "%~dp0env.bat" call "%~dp0env.bat"

REM Clean if requested (either position)
if /I "%~1"=="clean" set "DO_CLEAN=1"
if /I "%~2"=="clean" set "DO_CLEAN=1"

REM Configuration
if "%~1"=="" (
    set "CONFIGURATION=Release"
) else if /I not "%~1"=="clean" (
    set "CONFIGURATION=%~1"
)
if /I not "%CONFIGURATION%"=="Debug" if /I not "%CONFIGURATION%"=="Release" (
    echo Unknown configuration "%CONFIGURATION%", defaulting to Release.
    set "CONFIGURATION=Release"
)

REM Get root
set "ROOT_DIR=%~dp0"
REM Remove trailing backslash if present
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "BUILD_DIR=%ROOT_DIR%\build"

if defined DO_CLEAN (
    echo Cleaning build directory...
    rmdir /s /q "%BUILD_DIR%"
)

if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

REM Get Visual Studio dev environment using vswhere
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set "VS_PATH=%%i"
)
echo Found Visual Studio at: !VS_PATH!
call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64

REM Configure CMake
set "GENERATOR=Visual Studio 18 2026"
echo Configuring CMake with generator: !GENERATOR! and configuration: !CONFIGURATION!
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "!GENERATOR!" -A x64
if errorlevel 1 (
    echo Failed to configure CMake with generator: !GENERATOR! and configuration: !CONFIGURATION!
    echo Trying to configure CMake with generic generator.
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "Visual Studio" -A x64
    if errorlevel 1 (
        echo Failed to configure CMake with generic generator.
        exit /b 1
    )
)

REM Build
echo Building with configuration: !CONFIGURATION!
cmake --build "%BUILD_DIR%" --config "!CONFIGURATION!" -- /m
if errorlevel 1 (
    echo Build failed with configuration: !CONFIGURATION!
    exit /b 1
)

echo Build succeeded with configuration: !CONFIGURATION!
echo !CONFIGURATION!>"%~dp0.build_last_config"
exit /b 0