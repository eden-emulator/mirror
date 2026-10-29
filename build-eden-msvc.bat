@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
if errorlevel 1 goto :failed

set "SOURCE_DIR=%~dp0"
if not exist "%SOURCE_DIR%CMakeLists.txt" (
    echo ERROR: Put this file in the Eden source folder beside CMakeLists.txt.
    goto :failed
)
set "BUILD_DIR=%SOURCE_DIR%build-msvc-gui-relwithdebinfo"
set "BUILD_JOBS=6"
if not "%~1"=="" set "BUILD_JOBS=%~1"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Installer was not found.
    echo Install Visual Studio or Build Tools with Desktop development with C++.
    goto :failed
)
set "VS_INSTALL="
for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%V"
if not defined VS_INSTALL (
    echo ERROR: No Visual Studio installation with MSVC x64 tools was found.
    goto :failed
)
echo Using Visual Studio: %VS_INSTALL%
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 goto :failed

set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" (
    echo ERROR: Native Windows CMake was not found.
    echo Install CMake for Windows or Visual Studio's C++ CMake tools for Windows.
    goto :failed
)
set "NINJA_EXE=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%NINJA_EXE%" set "NINJA_EXE=%ProgramFiles%\CMake\bin\ninja.exe"
if not exist "%NINJA_EXE%" (
    echo ERROR: Ninja was not found. Install Visual Studio's C++ CMake tools for Windows.
    goto :failed
)

set "GIT_ROOT=%ProgramFiles%\Git"
if not exist "%GIT_ROOT%\usr\bin\patch.exe" (
    set "GIT_EXE="
    for /f "delims=" %%G in ('where git.exe 2^>nul') do if not defined GIT_EXE set "GIT_EXE=%%G"
    if not defined GIT_EXE (
        echo ERROR: Git for Windows was not found. Install it with its command-line tools.
        goto :failed
    )
    call :find_git_root
)
if not exist "%GIT_ROOT%\usr\bin\patch.exe" (
    echo ERROR: Git for Windows' usr\bin\patch.exe was not found.
    goto :failed
)
set "PATH=%GIT_ROOT%\cmd;%GIT_ROOT%\usr\bin;%PATH%"
set "CC=cl.exe"
set "CXX=cl.exe"
set "CMAKE_GENERATOR=Ninja"
set "CMAKE_GENERATOR_PLATFORM="
set "CMAKE_GENERATOR_TOOLSET="

if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /x /c:"CMAKE_GENERATOR:INTERNAL=Ninja" "%BUILD_DIR%\CMakeCache.txt" >nul
    if errorlevel 1 (
        echo ERROR: The GUI build folder contains another generator's cache.
        echo Rename "%BUILD_DIR%" and run this file again.
        goto :failed
    )
)

echo.
echo Initializing submodules...
git -C "%SOURCE_DIR%." submodule update --init --recursive
if errorlevel 1 goto :failed

echo.
echo Configuring GUI - MSVC x64, RelWithDebInfo...
"%CMAKE_EXE%" -S "%SOURCE_DIR%." -B "%BUILD_DIR%" -G Ninja ^
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=%NINJA_EXE%" ^
    -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DENABLE_QT=ON -DYUZU_USE_BUNDLED_QT=ON ^
    -DYUZU_CMD=OFF -DYUZU_ROOM=OFF ^
    -DBUILD_TESTING=OFF -DYUZU_TESTS=OFF -DDYNARMIC_TESTS=OFF ^
    -DENABLE_CUBEB=OFF -DENABLE_RESHADE=OFF ^
    -DENABLE_OPENGL=OFF -DENABLE_WEB_SERVICE=OFF ^
    -DENABLE_LTO=OFF -DYUZU_DISABLE_LLVM=ON -DENABLE_WERROR=OFF
if errorlevel 1 goto :failed

echo.
echo Building Eden GUI...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config RelWithDebInfo --target yuzu --parallel "%BUILD_JOBS%"
if errorlevel 1 goto :failed

echo.
echo Build complete: "%BUILD_DIR%\bin\eden.exe"
echo Keep the DLLs and plugins folder beside eden.exe.
pause
exit /b 0

:find_git_root
for %%G in ("%GIT_EXE%") do set "GIT_ROOT=%%~dpG.."
exit /b 0

:failed
echo.
echo Build failed. Read the first error above.
pause
exit /b 1
