@echo off
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo MSVC vcvars32.bat not found at %VCVARS%
    pause
    exit /b 1
)

call "%VCVARS%"
cd /d "%~dp0"

cl /nologo /LD /MT /O2 /GS- /EHsc /utf-8 AG_DefGenerators.cpp /link /DEF:AG_DefGenerators.def /OUT:AG_DefGenerators.dll

if exist AG_DefGenerators.dll (
    copy /Y AG_DefGenerators.dll "..\DATA\AG_DefGenerators.dll"
    echo Build and copy successful!
) else (
    echo Build failed!
)
