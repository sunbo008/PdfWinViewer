@echo on
setlocal
REM Robust build script: auto-detect VS DevCmd and CMake; generate project if missing; then build.

REM 1) Close running instance to avoid LNK1168
taskkill /f /im PdfWinViewer.exe /t >nul 2>&1

REM 2) Locate VsDevCmd.bat
set "VSDEVCMD="
for %%E in (Enterprise Professional Community BuildTools) do (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat"
)
if not defined VSDEVCMD (
  set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSROOT=%%i"
    if defined VSROOT if exist "%VSROOT%\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%VSROOT%\Common7\Tools\VsDevCmd.bat"
  )
)
if not defined VSDEVCMD (
  echo [ERROR] Could not locate VsDevCmd.bat. Please install Visual Studio 2022.
  exit /b 1
)
call "%VSDEVCMD%" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1

REM 3) Ensure CMake available (try PATH, then VS-bundled, then Program Files)
set "CMAKE=cmake"
where %CMAKE% >nul 2>&1
if errorlevel 1 (
  if defined VSROOT if exist "%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
where %CMAKE% >nul 2>&1
if errorlevel 1 (
  if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
)
where %CMAKE% >nul 2>&1
if errorlevel 1 (
  echo [ERROR] CMake not found. Please install CMake or add it to PATH.
  exit /b 1
)

REM 4) Configure via CMake (idempotent)
REM Avoid trailing backslash escaping the closing quote by appending a dot
set "SRCDIR=%~dp0."
set "BUILDDIR=%~dp0build"
if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
set "PROJ=%BUILDDIR%\PdfWinViewer.vcxproj"
set "OUTEXE=%BUILDDIR%\Debug\PdfWinViewer.exe"
set "DEFAULT_PDFIUM_ROOT=%~dp0..\pdfium"
if not defined PDFIUM_ROOT set "PDFIUM_ROOT=%DEFAULT_PDFIUM_ROOT%"
set "PDFIUM_OUT=%PDFIUM_ROOT%\out\XFA"
echo [INFO] Configuring with PDFIUM_ROOT=%PDFIUM_ROOT% PDFIUM_OUT=%PDFIUM_OUT%
"%CMAKE%" -S "%SRCDIR%" -B "%BUILDDIR%" -G "Visual Studio 17 2022" -A x64 -DPDFIUM_ROOT="%PDFIUM_ROOT%" -DPDFIUM_OUT="%PDFIUM_OUT%"
if errorlevel 1 exit /b 1

REM 5) Build
msbuild "%PROJ%" /m /p:Configuration=Debug /p:Platform=x64 /t:Build /nologo /v:minimal
if errorlevel 1 (
  echo BUILD FAILED (msbuild error)
  exit /b 1
)
if exist "%OUTEXE%" (
  echo BUILD OK
) else (
  echo BUILD FAILED
  exit /b 1
)

endlocal
exit /b 0
