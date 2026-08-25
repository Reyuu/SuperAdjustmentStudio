@echo off
REM deploy.bat [build_dir] [deploy_dir]

setlocal
if not defined DEPLOY_DIR if exist "%~dp0env.bat" call "%~dp0env.bat"

if "%~1"=="" (
    if exist "%~dp0.build_last_config" (
        for /f "usebackq" %%c in ("%~dp0.build_last_config") do set "CONFIGURATION=%%c"
    ) else (
        set "CONFIGURATION=Release"
    )
) else (
    set "CONFIGURATION=%~1"
)
if /I not "%CONFIGURATION%"=="Debug" if /I not "%CONFIGURATION%"=="Release" (
    echo Unknown configuration "%CONFIGURATION%", defaulting to Release.
    set "CONFIGURATION=Release"
)

if "%~2"=="" (
    set "BUILD_DIR=%~dp0build\%CONFIGURATION%"
) else (
    set "BUILD_DIR=%~2"
)

if "%~3"=="" (
    set "DEPLOY_DIR=%DEPLOY_DIR%"
) else (
    set "DEPLOY_DIR=%~3"
)

echo Deploying from directory: %BUILD_DIR%
echo Deploying to directory: %DEPLOY_DIR%

rem Check if the build directory exists
if not exist "%BUILD_DIR%" (
    echo Build directory does not exist: %BUILD_DIR%
    exit /b 1
)

rem Check if the deployment directory exists
if not exist "%DEPLOY_DIR%" (
    echo Deployment directory does not exist: %DEPLOY_DIR%
    exit /b 1
)

rem Rename all .dll files in the build directory to .asi
for /f "tokens=*" %%f in ('dir /b "%BUILD_DIR%\*.dll"') do (
    set "filename=%%~nf"
    setlocal enabledelayedexpansion
    ren "%BUILD_DIR%\%%f" "!filename!.asi"
    endlocal
)

rem Copy .asi files from the build directory to the deployment directory
copy "%BUILD_DIR%\*.asi" "%DEPLOY_DIR%"

rem For Debug, also copy the PDB so the debugger can resolve symbols
if /I "%CONFIGURATION%"=="Debug" (
    copy "%BUILD_DIR%\SAS_SuperAdjustmentStudio.pdb" "%DEPLOY_DIR%"
    if errorlevel 1 (
        echo Failed to copy PDB file.
        exit /b 1
    )
)

rem make sure the copy commands succeeded
if errorlevel 1 (
    echo Deployment failed.
    exit /b 1
)

echo Deployment succeeded.
exit /b 0