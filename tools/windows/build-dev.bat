@echo off
REM =====================================================================
REM  tools\windows\build-dev.bat — HobbyCAD Developer Build Script
REM =====================================================================
REM
REM  Configures and builds HobbyCAD for day-to-day development.
REM  All output is logged to build-hobbycad.log in the project root
REM  and also displayed on the terminal.
REM
REM  Parameters are processed left to right:
REM
REM    tools\windows\build-dev.bat                     Debug build
REM    tools\windows\build-dev.bat release             Release build
REM    tools\windows\build-dev.bat clean               Clean only (no build)
REM    tools\windows\build-dev.bat clean debug         Clean + Debug build
REM    tools\windows\build-dev.bat clean release       Clean + Release build
REM    tools\windows\build-dev.bat run                 Run if built,
REM                                                      else build + run
REM    tools\windows\build-dev.bat run-reduced         Run in Reduced Mode
REM    tools\windows\build-dev.bat run-cli             Run in CLI mode
REM    tools\windows\build-dev.bat clean run           Clean + build + run
REM    tools\windows\build-dev.bat clean release run   Clean + Release + run
REM
REM  "run" behavior:
REM    - If build dir and binary exist: just launch (no build)
REM    - If either is missing: devtest check, build, then launch
REM    - "clean" before "run" forces a full rebuild
REM
REM  Prerequisites:
REM    - CMake on PATH
REM    - Ninja on PATH (recommended) or Visual Studio generator
REM    - Qt 6 installed, CMAKE_PREFIX_PATH or Qt6_DIR set
REM    - OCCT installed, OpenCASCADE_DIR set
REM    - Run from a Developer Command Prompt or ensure cl.exe is on PATH
REM
REM  SPDX-License-Identifier: GPL-3.0-only
REM
REM =====================================================================

setlocal enabledelayedexpansion

REM ---- Resolve paths --------------------------------------------------

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%\..\.."
set "PROJECT_ROOT=%CD%"
popd

set "BUILD_DIR=%PROJECT_ROOT%\build"
set "LOG=%PROJECT_ROOT%\build-hobbycad.log"

set "DEVTEST_DIR=%PROJECT_ROOT%\devtest"
set "DEVTEST_LOG=%DEVTEST_DIR%\devtest.log"

REM ---- Detect generator -----------------------------------------------

where ninja >nul 2>&1
if %errorlevel%==0 (
    set "GENERATOR=Ninja"
) else (
    set "GENERATOR=Visual Studio 17 2022"
)

REM ---- Detect binary path (depends on generator) ----------------------

if "!GENERATOR!"=="Ninja" (
    set "BINARY=%BUILD_DIR%\src\hobbycad\hobbycad.exe"
) else (
    set "BINARY=%BUILD_DIR%\src\hobbycad\%BUILD_TYPE%\hobbycad.exe"
)

REM ---- Parse arguments (collect actions in order) ---------------------

set "BUILD_TYPE=Debug"
set "ACTION_COUNT=0"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="release" set "BUILD_TYPE=Release" & shift & goto parse_args
if /i "%~1"=="debug"   set "BUILD_TYPE=Debug"   & shift & goto parse_args
if /i "%~1"=="clean" (
    set /a ACTION_COUNT+=1
    set "ACTION_!ACTION_COUNT!=clean"
    shift & goto parse_args
)
if /i "%~1"=="run" (
    set /a ACTION_COUNT+=1
    set "ACTION_!ACTION_COUNT!=run"
    shift & goto parse_args
)
if /i "%~1"=="run-reduced" (
    set /a ACTION_COUNT+=1
    set "ACTION_!ACTION_COUNT!=run-reduced"
    shift & goto parse_args
)
if /i "%~1"=="run-cli" (
    set /a ACTION_COUNT+=1
    set "ACTION_!ACTION_COUNT!=run-cli"
    shift & goto parse_args
)
echo Usage: %~nx0 [debug^|release] [clean] [run^|run-reduced^|run-cli]
exit /b 1
:args_done

REM Default action: build
if %ACTION_COUNT%==0 (
    set "ACTION_COUNT=1"
    set "ACTION_1=build"
)

REM ---- Clear log file -------------------------------------------------

echo. > "%LOG%"

set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"

call :log "====================================================================="
call :log "  HobbyCAD Developer Build"
call :log "====================================================================="
call :log ""
call :log "  Project root : %PROJECT_ROOT%"
call :log "  Build dir    : %BUILD_DIR%"
call :log "  Build type   : %BUILD_TYPE%"
call :log "  Generator    : !GENERATOR!"
call :log "  Log file     : %LOG%"
call :log "  Date         : %DATE% %TIME%"
call :log ""

REM ---- Execute actions left to right ----------------------------------

set "DID_RUN=false"
for /l %%I in (1,1,%ACTION_COUNT%) do (
    if "!ACTION_%%I!"=="clean"       call :do_clean
    if "!ACTION_%%I!"=="build"       call :do_build
    if "!ACTION_%%I!"=="run"         call :do_run
    if "!ACTION_%%I!"=="run-reduced" call :do_run_reduced
    if "!ACTION_%%I!"=="run-cli"     call :do_run_cli
)

REM Show usage hints if no run was requested
if "%DID_RUN%"=="false" (
    if exist "!BINARY!" (
        call :log "  Run:"
        call :log "    !BINARY!"
        call :log ""
        call :log "  Reduced Mode (testing):"
        call :log "    set HOBBYCAD_REDUCED_MODE=1 && !BINARY!"
        call :log ""
        call :log "  CLI Mode:"
        call :log "    !BINARY! --no-gui"
        call :log ""
    )
)

call :log "Log written to %LOG%"
exit /b 0

REM =====================================================================
REM  Helper subroutines
REM =====================================================================

:do_devtest
    set "DEVTEST_NEEDED=true"

    if exist "%DEVTEST_LOG%" (
        set "DEVTEST_FOUND="
        for /f "usebackq delims=" %%L in (`findstr /b /c:"DEVTEST_RESULT:" "%DEVTEST_LOG%" ^| findstr "[PASS]"`) do (
            set "DEVTEST_FOUND=%%L"
        )
        if defined DEVTEST_FOUND (
            set "DEVTEST_NEEDED=false"
            set "DEVTEST_DISPLAY=!DEVTEST_FOUND:DEVTEST_RESULT: =!"
            call :log "--- Devtest: !DEVTEST_DISPLAY! (skipping) ---"
            call :log "  Log: %DEVTEST_LOG%"
            call :log ""
        ) else (
            call :log "--- Devtest: result line missing or failed — rerunning ---"
            call :log ""
        )
    )

    if "%DEVTEST_NEEDED%"=="false" goto :eof

    call :log "--- Running devtest ---"
    call :log ""

    if not exist "%DEVTEST_DIR%" (
        call :log "  WARNING: devtest\ directory not found — skipping"
        call :log ""
        goto :eof
    )

    pushd "%DEVTEST_DIR%"
    cmake -B build >> "%LOG%" 2>&1
    if %errorlevel% neq 0 (
        call :log "  Devtest configure FAILED"
        popd
        exit /b 1
    )
    cmake --build build -j %JOBS% >> "%LOG%" 2>&1
    if %errorlevel% neq 0 (
        call :log "  Devtest build FAILED"
        popd
        exit /b 1
    )
    build\depcheck.exe >> "%DEVTEST_LOG%" 2>&1
    if %errorlevel% neq 0 (
        call :log "  Devtest FAILED — fix dependency issues before building."
        call :log "  See: %DEVTEST_LOG%"
        popd
        exit /b 1
    )
    popd
    call :log "  Devtest passed"
    call :log ""
goto :eof

:do_clean
    call :log "--- Cleaning build directory ---"
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%"
        call :log "  Removed: %BUILD_DIR%"
    ) else (
        call :log "  Nothing to clean"
    )
    call :log ""
goto :eof

:do_build
    call :do_devtest

    call :log "--- Configuring (CMake) ---"
    call :log ""

    REM Auto-detect preset family: msvc if VCPKG_ROOT is set, else msys2
    if defined VCPKG_ROOT (
        set "PRESET_FAMILY=msvc"
    ) else (
        set "PRESET_FAMILY=msys2"
    )

    REM Build type to lowercase for preset name
    set "BT_LOWER=%BUILD_TYPE%"
    if /i "%BUILD_TYPE%"=="Debug"   set "BT_LOWER=debug"
    if /i "%BUILD_TYPE%"=="Release" set "BT_LOWER=release"

    set "PRESET=!PRESET_FAMILY!-!BT_LOWER!"

    cmake --preset "!PRESET!" -S "%PROJECT_ROOT%" >> "%LOG%" 2>&1

    if %errorlevel% neq 0 (
        call :log "  CMake configure FAILED — see %LOG%"
        exit /b 1
    )
    call :log ""

    call :log "--- Building (%JOBS% jobs) ---"
    call :log ""

    cmake --build --preset "!PRESET!" -j %JOBS% >> "%LOG%" 2>&1

    if %errorlevel% neq 0 (
        call :log "====================================================================="
        call :log "  Build FAILED"
        call :log "====================================================================="
        call :log "  Check the log: %LOG%"
        exit /b 1
    )
    call :log ""

    if exist "!BINARY!" (
        call :log "====================================================================="
        call :log "  Build successful"
        call :log "====================================================================="
        call :log ""
        call :log "  Binary : !BINARY!"
        call :log "  Type   : %BUILD_TYPE%"
        call :log ""
    ) else (
        call :log "====================================================================="
        call :log "  Build FAILED — binary not found"
        call :log "====================================================================="
        call :log "  Expected: !BINARY!"
        call :log "  Check the log: %LOG%"
        exit /b 1
    )
goto :eof

:do_run
    set "DID_RUN=true"

    REM If binary exists, just launch it
    if exist "!BINARY!" (
        call :log "--- Launching HobbyCAD ---"
        call :log ""
        start "" "!BINARY!"
        call :log "  Launched in new process"
        call :log ""
        goto :eof
    )

    REM Binary missing — build first
    call :log "--- Binary not found — building first ---"
    call :log ""
    call :do_build

    call :log "--- Launching HobbyCAD ---"
    call :log ""
    start "" "!BINARY!"
    call :log "  Launched in new process"
    call :log ""
goto :eof

:do_run_reduced
    set "DID_RUN=true"

    if exist "!BINARY!" (
        call :log "--- Launching HobbyCAD Reduced Mode ---"
        call :log ""
        set "HOBBYCAD_REDUCED_MODE=1"
        start "" "!BINARY!"
        call :log "  Launched in new process"
        call :log ""
        goto :eof
    )

    call :log "--- Binary not found — building first ---"
    call :log ""
    call :do_build

    call :log "--- Launching HobbyCAD Reduced Mode ---"
    call :log ""
    set "HOBBYCAD_REDUCED_MODE=1"
    start "" "!BINARY!"
    call :log "  Launched in new process"
    call :log ""
goto :eof

:do_run_cli
    set "DID_RUN=true"

    if exist "!BINARY!" (
        call :log "--- Launching HobbyCAD CLI mode ---"
        call :log ""
        "!BINARY!" --no-gui
        goto :eof
    )

    call :log "--- Binary not found — building first ---"
    call :log ""
    call :do_build

    call :log "--- Launching HobbyCAD CLI mode ---"
    call :log ""
    "!BINARY!" --no-gui
goto :eof

REM ---- Log helper -----------------------------------------------------
:log
echo %~1
if "%~1"=="" (
    echo.>> "%LOG%"
) else (
    echo %~1>> "%LOG%"
)
goto :eof
