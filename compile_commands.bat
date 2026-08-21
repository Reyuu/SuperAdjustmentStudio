@echo off
REM compile_commands.bat - regenerate compile_commands.json for clangd
REM The VS generator cannot export compile commands, so we do a second
REM configure with the NMake Makefiles generator (uses only VS tooling).

setlocal enabledelayedexpansion

REM Get root
set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "BUILD_DIR=%ROOT_DIR%\build\compile-commands"

REM Get Visual Studio dev environment using vswhere
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set "VS_PATH=%%i"
)
echo Found Visual Studio at: !VS_PATH!
call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64

set "CC_CONFIG=%~1"
if "%CC_CONFIG%"=="" set "CC_CONFIG=Release"

REM Configure (no build - only the compile database is needed)
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%CC_CONFIG% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 (
    echo Failed to configure compile database.
    exit /b 1
)

REM clangd auto-discovers compile_commands.json in the project root
copy /y "%BUILD_DIR%\compile_commands.json" "%ROOT_DIR%\compile_commands.json" >nul
if errorlevel 1 exit /b 1

echo compile_commands.json written to %ROOT_DIR%\compile_commands.json
exit /b 0
