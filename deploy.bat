@echo off
REM deploy.bat [build_dir] [deploy_dir]

setlocal
call env.bat

if "%~1"=="" (
    set "BUILD_DIR=%~dp0build\Release"
) else (
    set "BUILD_DIR=%~1"
)

if "%~2"=="" (
    set "DEPLOY_DIR=%DEPLOY_DIR%"
) else (
    set "DEPLOY_DIR=%~2"
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

rem make sure the copy commands succeeded
if errorlevel 1 (
    echo Deployment failed.
    exit /b 1
)

echo Deployment succeeded.
exit /b 0