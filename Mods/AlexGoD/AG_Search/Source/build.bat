@echo off
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo MSVC vcvars32.bat not found at %VCVARS%
    pause
    exit /b 1
)

call "%VCVARS%"
cd /d "%~dp0"

cl /nologo /LD /MT /O2 /GS- /EHsc AG_Search.cpp /link /DEF:AG_Search.def /OUT:AG_Search.dll

if exist AG_Search.dll (
    copy /Y AG_Search.dll "..\DATA\AG_Search.dll"
    echo Build and copy successful!
) else (
    echo Build failed!
)
