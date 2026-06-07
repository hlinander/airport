@echo off
REM Build script for Airport DuckDB extension on Windows with Visual Studio 2022
REM Usage: build_windows.bat [debug|release]

set BUILD_TYPE=Release
if /I "%1"=="debug" set BUILD_TYPE=Debug

echo === Building Airport Extension (%BUILD_TYPE%) ===

REM Set up Visual Studio 2022 environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

REM Redirect TEMP to D: drive (C: has very little space)
if not exist "D:\Temp" mkdir "D:\Temp"
set TEMP=D:\Temp
set TMP=D:\Temp

REM Limit vcpkg build parallelism to 32 threads
set VCPKG_MAX_CONCURRENCY=32

REM Use absolute paths from project root
cd /d "%~dp0"
echo Working directory: %CD%

set VCPKG_TC=%CD%\vcpkg\scripts\buildsystems\vcpkg.cmake
set EXT_CFG=%CD%\extension_config.cmake
set PROJ=%CD%
REM Convert backslashes to forward slashes for CMake path defines
set PROJ_CMAKE=%PROJ:\=/%

if not exist "%VCPKG_TC%" (
    echo ERROR: vcpkg toolchain not found at %VCPKG_TC%
    exit /b 1
)

if not exist "build\%BUILD_TYPE%" mkdir "build\%BUILD_TYPE%"

echo.
echo === CMake Configure ===
echo Toolchain: %VCPKG_TC%
echo TEMP: %TEMP%

REM Always generate debug symbols (PDB files) even in Release builds.
REM /Zi produces a separate PDB; /DEBUG on the linker emits it.
REM /OPT:REF and /OPT:ICF keep the binary size small despite /DEBUG.
cmake -G "NMake Makefiles" -DEXTENSION_STATIC_BUILD=1 -DDUCKDB_EXTENSION_CONFIGS="%EXT_CFG%" -DUNITTEST_ROOT_DIRECTORY="%PROJ_CMAKE%/" -DBENCHMARK_ROOT_DIRECTORY="%PROJ_CMAKE%/" -DENABLE_UNITTEST_CPP_TESTS=FALSE -DENABLE_EXTENSION_AUTOLOADING=0 -DENABLE_EXTENSION_AUTOINSTALL=0 -DVCPKG_BUILD=1 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TC%" -DVCPKG_MANIFEST_DIR="%PROJ%" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_C_FLAGS_RELEASE="/Zi /O2 /DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="/Zi /O2 /DNDEBUG" -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/DEBUG /OPT:REF /OPT:ICF" -DCMAKE_SHARED_LINKER_FLAGS_RELEASE="/DEBUG /OPT:REF /OPT:ICF" -DCMAKE_MODULE_LINKER_FLAGS_RELEASE="/DEBUG /OPT:REF /OPT:ICF" -S duckdb -B build\%BUILD_TYPE%

if errorlevel 1 (
    echo ERROR: CMake configure failed
    exit /b 1
)

echo.
echo === CMake Build ===
cmake --build build\%BUILD_TYPE% --config %BUILD_TYPE%

if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

echo.
echo === Build Complete ===
