@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem --- locate a Visual Studio / Build Tools installation -------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [!] vswhere.exe not found - install Visual Studio Build Tools with the
    echo     "Desktop development with C++" workload and the Windows SDK.
    exit /b 1
)

set "VSARGS=-latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" %VSARGS%`) do set "VSPATH=%%i"

if not defined VSPATH (
    echo [!] No VC++ toolset found.
    exit /b 1
)

rem (VS 18's vcvars64.bat writes a harmless "vswhere.exe not recognized" to stderr)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 exit /b 1

rem --- compile ------------------------------------------------------------
if exist K380_FN_switch.exe del K380_FN_switch.exe

rc /nologo /fo app.res app.rc || exit /b 1

rem /O1 /Os      optimise for size
rem /GS-         no stack cookie (pulls in the CRT otherwise)
rem /GR- /Zc:*   no RTTI, strict conformance
rem /NODEFAULTLIB + /ENTRY  build without the C runtime (~15 KB instead of 157 KB)
cl /nologo /W4 /O1 /Os /GS- /GR- /Gy /permissive- ^
   /D_UNICODE /DUNICODE /DNDEBUG /DK380_NO_CRT ^
   K380_FN_switch.cpp app.res ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:AppEntryPoint /NODEFAULTLIB ^
   /OPT:REF /OPT:ICF /INCREMENTAL:NO /RELEASE ^
   kernel32.lib user32.lib gdi32.lib shell32.lib advapi32.lib ^
   setupapi.lib hid.lib wtsapi32.lib ^
   /OUT:K380_FN_switch.exe || exit /b 1

del /q *.obj app.res 2>nul

for %%F in (K380_FN_switch.exe) do echo [ok] %%~nxF  %%~zF bytes
endlocal
