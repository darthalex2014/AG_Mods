@echo off
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo MSVC vcvars32.bat not found at %VCVARS%
    exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
    echo Failed to initialize MSVC x86 environment
    exit /b 1
)

set "OUT_DIR=..\DATA"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

cl /nologo /O2 /utf-8 /LD /Fe:"%OUT_DIR%\AG_Casino.dll" AG_Casino.cpp /link /DEF:AG_Casino.def User32.lib /MACHINE:X86 /OPT:REF /OPT:ICF

if errorlevel 1 (
    echo Build FAILED!
    exit /b 1
)

echo Build SUCCESS: %OUT_DIR%\AG_Casino.dll
if exist AG_Casino.obj del AG_Casino.obj
if exist "%OUT_DIR%\AG_Casino.exp" del "%OUT_DIR%\AG_Casino.exp"
if exist "%OUT_DIR%\AG_Casino.lib" del "%OUT_DIR%\AG_Casino.lib"
