@echo off
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo MSVC vcvars32.bat not found at %VCVARS%
    pause
    exit /b 1
)

call "%VCVARS%"
cd /d "%~dp0"

cl /nologo /LD /MT /O2 /GS- /EHsc /utf-8 AG_FuelTanks.cpp /link /DEF:AG_FuelTanks.def /OUT:AG_FuelTanks.dll

if exist AG_FuelTanks.dll (
    copy /Y AG_FuelTanks.dll "..\DATA\AG_FuelTanks.dll"
    echo Build and copy successful!
) else (
    echo Build failed!
)
