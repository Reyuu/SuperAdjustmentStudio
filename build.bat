@echo off
REM build.bat [Debug|Release] [clean] [ME1|ME2|ME3]

setlocal enabledelayedexpansion
if exist "%~dp0env.bat" call "%~dp0env.bat"

set "CONFIGURATION=Release"
set "DO_CLEAN=0"
set "SINGLE_GAME="
for %%A in (%*) do (
    if /I "%%A"=="clean" set "DO_CLEAN=1"
    if /I "%%A"=="Debug" set "CONFIGURATION=Debug"
    if /I "%%A"=="Release" set "CONFIGURATION=Release"
    if /I "%%A"=="ME1" set "SINGLE_GAME=ME1"
    if /I "%%A"=="ME2" set "SINGLE_GAME=ME2"
    if /I "%%A"=="ME3" set "SINGLE_GAME=ME3"
)

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

if "!DO_CLEAN!"=="1" (
    if defined SINGLE_GAME (
        echo Cleaning build directory for !SINGLE_GAME!...
        rmdir /s /q "%ROOT_DIR%\build\!SINGLE_GAME!" 2>nul
    ) else (
        echo Cleaning build directory...
        rmdir /s /q "%ROOT_DIR%\build"
    )
)

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set "VS_PATH=%%i"
)
echo Found Visual Studio at: !VS_PATH!
call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64

set "GAMES=ME1 ME2 ME3"
if defined SINGLE_GAME set "GAMES=!SINGLE_GAME!"

set "FAIL_COUNT=0"
for %%G in (!GAMES!) do (
    set "GAME=%%G"
    set "BUILD_DIR=%ROOT_DIR%\build\!GAME!"

    echo.
    echo ===== Building !GAME! (!CONFIGURATION!^) =====
    if not exist "!BUILD_DIR!" mkdir "!BUILD_DIR!"

    cmake -S "%ROOT_DIR%" -B "!BUILD_DIR!" -G "Visual Studio 18 2026" -A x64 -DSAS_GAME=!GAME! >nul 2>nul
    if errorlevel 1 cmake -S "%ROOT_DIR%" -B "!BUILD_DIR!" -G "Visual Studio" -A x64 -DSAS_GAME=!GAME! >nul 2>nul
    if errorlevel 1 (
        echo FAILED: CMake configure for !GAME!
        set /a FAIL_COUNT+=1
    ) else (
        cmake --build "!BUILD_DIR!" --config "!CONFIGURATION!" -- /m
        if errorlevel 1 (
            echo FAILED: Build for !GAME!
            set /a FAIL_COUNT+=1
        ) else (
            echo !GAME! build succeeded.
            echo !CONFIGURATION!>"%~dp0.build_last_config_!GAME!"
        )
    )
)

if !FAIL_COUNT! GTR 0 (
    echo.
    echo !FAIL_COUNT! game^(s^) failed to build.
    exit /b 1
)

echo.
echo All games built successfully with configuration: !CONFIGURATION!
echo !CONFIGURATION!>"%~dp0.build_last_config"
exit /b 0
