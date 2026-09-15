@echo off
REM deploy.bat [Debug|Release] [ME1|ME2|ME3]

setlocal enabledelayedexpansion
if exist "%~dp0env.bat" call "%~dp0env.bat"

REM Parse arguments
set "CONFIGURATION="
set "SINGLE_GAME="
for %%A in (%*) do (
    if /I "%%A"=="Debug" (
        set "CONFIGURATION=Debug"
    ) else if /I "%%A"=="Release" (
        set "CONFIGURATION=Release"
    ) else if /I "%%A"=="ME1" (
        set "SINGLE_GAME=ME1"
    ) else if /I "%%A"=="ME2" (
        set "SINGLE_GAME=ME2"
    ) else if /I "%%A"=="ME3" (
        set "SINGLE_GAME=ME3"
    )
)

if "%CONFIGURATION%"=="" (
    if exist "%~dp0.build_last_config" (
        for /f "usebackq" %%c in ("%~dp0.build_last_config") do set "CONFIGURATION=%%c"
    ) else (
        set "CONFIGURATION=Release"
    )
)

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "GAMES=ME1 ME2 ME3"
if defined SINGLE_GAME set "GAMES=!SINGLE_GAME!"

set "FAIL=0"

for %%G in (!GAMES!) do (
    set "GAME=%%G"
    set "BUILD_DIR=%ROOT_DIR%\build\%%G\!CONFIGURATION!"
    set "DEPLOY_DIR=!DEPLOY_DIR_%%G!"

    echo.
    echo ===== Deploying !GAME! (!CONFIGURATION!) =====

    if not exist "!BUILD_DIR!" (
        echo Build directory does not exist: !BUILD_DIR!
        set "FAIL=1"
    ) else if not defined DEPLOY_DIR (
        echo DEPLOY_DIR_!GAME! not set, skipping.
    ) else if not exist "!DEPLOY_DIR!" (
        echo Deployment directory does not exist: !DEPLOY_DIR!
        set "FAIL=1"
    ) else (
        for /f "tokens=*" %%f in ('dir /b "!BUILD_DIR!\*.dll" 2^>nul') do (
            set "filename=%%~nf"
            if exist "!BUILD_DIR!\!filename!.asi" del "!BUILD_DIR!\!filename!.asi"
            ren "!BUILD_DIR!\%%f" "!filename!.asi"
        )

        copy "!BUILD_DIR!\*.asi" "!DEPLOY_DIR!" >nul

        if /I "!CONFIGURATION!"=="Debug" (
            copy "!BUILD_DIR!\SAS_SuperAdjustmentStudio.pdb" "!DEPLOY_DIR!" >nul 2>nul
        )

        if errorlevel 1 (
            echo Deployment failed for !GAME!.
            set "FAIL=1"
        ) else (
            echo !GAME! deployed successfully.
        )
    )
)

if "!FAIL!"=="1" (
    echo.
    echo Some deployments failed.
    exit /b 1
)

echo.
echo All games deployed successfully.
exit /b 0
