@echo off
setlocal
rem Builds fork.exe and manager.exe (x64) and prints where they were created.
rem   build.bat           Release
rem   build.bat debug     Debug

set "RESULT=0"
set "CONFIGURATION=Release"
set "OUTPUT_NAME=release"
if /i "%~1"=="debug" (
    set "CONFIGURATION=Debug"
    set "OUTPUT_NAME=debug"
) else if not "%~1"=="" if /i not "%~1"=="release" (
    echo Unknown configuration "%~1": use debug or release.
    set "RESULT=1"
    goto :end
)

rem vswhere comes with the Visual Studio installer and knows where the newest MSBuild is.
rem It's run from its own folder, so the "(x86)" in that folder's name never needs quoting.
set "MSBUILD="
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer" 2>nul && (
    for /f "delims=" %%i in ('vswhere.exe -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe') do if not defined MSBUILD set "MSBUILD=%%i"
    popd
)
if not defined MSBUILD for /f "delims=" %%i in ('where msbuild.exe 2^>nul') do if not defined MSBUILD set "MSBUILD=%%i"
if not defined MSBUILD (
    echo MSBuild not found. Install Visual Studio ^(or its Build Tools^) with the "Desktop development with C++" workload.
    set "RESULT=1"
    goto :end
)

"%MSBUILD%" "%~dp0processes.slnx" /p:Configuration=%CONFIGURATION% /p:Platform=x64 /m /nologo /verbosity:minimal
if errorlevel 1 (
    set "RESULT=1"
    echo.
    echo Build failed.
    goto :end
)

echo.
echo Built (%CONFIGURATION%):
for %%e in (manager.exe fork.exe) do call :report "%~dp0bin\%OUTPUT_NAME%\%%e"

:end
echo.
pause
exit /b %RESULT%

:report
if not exist "%~1" goto :missing
echo   %~1
exit /b 0
:missing
echo   missing: %~1
set "RESULT=1"
exit /b 0
