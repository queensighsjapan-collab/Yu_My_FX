@echo off
setlocal
cd /d "%~dp0"
if "%YMECHO_RUN%"=="1" goto main
set YMECHO_RUN=1
call "%~f0" > build-log.txt 2>&1
echo.
echo Finished. A log file "build-log.txt" was saved in this folder.
echo If anything failed, please send build-log.txt to Manager.
pause
exit /b

:main
chcp 65001 >nul
echo ============================================
echo  YM Echo - automatic build
echo ============================================

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto findvs
echo [ERROR] Visual Studio installer was not found.
echo Please install Visual Studio 2022 Community (free) first,
echo and check "Desktop development with C++" during setup.
echo Download: https://visualstudio.microsoft.com/downloads/
goto end

:findvs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if defined VSPATH goto checkvc
echo Strict detection found nothing, trying fallback detection...
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
if defined VSPATH goto checkvc
:checkvc
if exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto setupenv
echo [ERROR] The C++ tools were not found in: %VSPATH%
echo [ERROR] Visual Studio with the C++ workload was not found.
echo Please start the Visual Studio Installer and add
echo "Desktop development with C++".
goto end

:setupenv
echo Visual Studio found: %VSPATH%
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VSVER=%%i"
for /f "tokens=1 delims=." %%a in ("%VSVER%") do set "VSMAJOR=%%a"
set "GEN=Visual Studio 17 2022"
if "%VSMAJOR%"=="18" set "GEN=Visual Studio 18 2026"
set "TOOLSET=v143"
if "%VSMAJOR%"=="18" set "TOOLSET=v145"
echo Using generator: %GEN%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"

where git >nul 2>nul
if not errorlevel 1 goto configure
set "GITDIR=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd"
if exist "%GITDIR%\git.exe" set "PATH=%GITDIR%;%PATH%"
where git >nul 2>nul
if not errorlevel 1 goto configure
echo [ERROR] Git was not found.
echo Please install Git for Windows (default options are fine):
echo https://git-scm.com/download/win
goto end

:configure
echo.
echo --- Step 1/2: preparing the project ---
echo (first run downloads JUCE, may take a few minutes)
cmake -B build -G "%GEN%" -A x64 -T %TOOLSET%
if errorlevel 1 goto cfgfail
goto build

:cfgfail
if "%YMECHO_RETRY%"=="1" goto cfgfail2
set YMECHO_RETRY=1
echo.
echo Retrying once with a clean build folder...
rmdir /s /q build
goto configure
:cfgfail2
echo.
echo [ERROR] Project preparation failed. Please send build-log.txt to Manager.
goto end

:build
echo.
echo --- Step 2/2: building (this can take several minutes) ---
cmake --build build --config Release
if errorlevel 1 goto buildfail
echo.
echo ============================================
echo  DONE!
echo  The plugin is here:
echo  build\YMEcho_artefacts\Release\VST3\YM Echo.vst3
echo.
echo  Next step: copy the whole "YM Echo.vst3" folder into
echo  C:\Program Files\Common Files\VST3
echo  (Windows will ask for administrator permission.)
echo ============================================
goto end

:buildfail
echo.
echo [ERROR] Build failed. Please send build-log.txt to Manager.
goto end

:end
exit /b
