@echo off
REM Builds the static NPC index generator (tools/npc_index).
REM Usage: build_npcindex.bat [ME2|ME3]   - defaults to ME2.
setlocal enabledelayedexpansion
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set "VS_PATH=%%i"
)
if not defined VS_PATH (
    echo VS not found.
    exit /b 1
)
call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d "%~dp0"

REM pick which game the index targets
set "GAME=ME2"
set "OUT=src\actors\npcs_index_le2.inc"
if /I "%~1"=="ME3" (
    set "GAME=ME3"
    set "OUT=src\actors\npcs_index_le3.inc"
)

echo index target: %GAME% to %OUT%

if not exist build\npc_index mkdir build\npc_index
cl /nologo /std:c++20 /EHsc /O2 /MT /Fo:build\npc_index\ /I tools\npc_index /I src\parse tools\npc_index\main.cpp src\parse\pcc_parser.cpp src\parse\oodle.cpp /Fe:build\npc_index\npc_index.exe
if errorlevel 1 exit /b 1
echo built build\npc_index\npc_index.exe for %GAME%
