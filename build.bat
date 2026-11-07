@echo off
rem build.bat - build Town3D from a plain shell, no IDE needed.
rem   build.bat                Release x64 + self-test
rem   build.bat Debug          Debug x64
rem   build.bat Release --run  build then launch the game (no self-test)
setlocal enabledelayedexpansion
set CFG=%1
if "%CFG%"=="" set CFG=Release
set HERE=%~dp0

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VSROOT=
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -property installationPath`) do set VSROOT=%%i
if "%VSROOT%"=="" set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community
set MSBUILD=%VSROOT%\MSBuild\Current\Bin\MSBuild.exe
if not exist "%MSBUILD%" (
  echo [build] MSBuild not found at "%MSBUILD%"
  echo [build] set VSROOT in this script, or just open Town3D.sln in the IDE.
  exit /b 1
)

rem The brief asks for Clang: use the ClangCL toolset whenever it is installed,
rem otherwise fall back to MSVC v145 (see LEDGER.md ISS-001).
set TOOLSET=
if exist "%VSROOT%\VC\Tools\Llvm\x64\bin\clang-cl.exe" set TOOLSET=-p:UseClang=true
if not defined TOOLSET (
  echo [build] no clang-cl under "%VSROOT%\VC\Tools\Llvm" -^> building with MSVC v145.
  echo [build] Want Clang? Visual Studio Installer -^> Modify -^> Individual components
  echo [build]   -^> "C++ Clang Compiler for Windows", then re-run this script.
)

echo [build] msbuild Town3D.sln -p:Configuration=%CFG% -p:Platform=x64 %TOOLSET%
"%MSBUILD%" "%HERE%Town3D.sln" -t:Rebuild -p:Configuration=%CFG% -p:Platform=x64 %TOOLSET% -m -v:m
if errorlevel 1 (
  echo [build] FAILED
  exit /b 1
)

echo.
echo [build] ok: %HERE%bin\Town3D.exe
if "%2"=="--run" (
  "%HERE%bin\Town3D.exe"
  exit /b 0
)

echo [build] self-test: shots\*.bmp + town3d.log
"%HERE%bin\Town3D.exe" --selftest --showlog
endlocal
