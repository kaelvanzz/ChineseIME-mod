@echo off
echo Building ChineseIME Native DLL...
echo.

REM 检查CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found. Please install CMake first.
    echo Download from: https://cmake.org/download/
    exit /b 1
)

REM 检查Visual Studio
where cl >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Visual Studio compiler not in PATH.
    echo Trying to find Visual Studio...
    
    REM 尝试查找VS 2022
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo ERROR: Visual Studio not found.
        echo Please install Visual Studio 2019 or 2022 with C++ workload.
        exit /b 1
    )
)

echo.
echo Creating build directory...
if not exist build mkdir build

echo.
echo Running CMake...
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed. Trying VS 2019...
    cmake -G "Visual Studio 16 2019" -A x64 ..
    if %ERRORLEVEL% neq 0 (
        echo CMake configuration failed.
        cd ..
        exit /b 1
    )
)

echo.
echo Building Release...
cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    cd ..
    exit /b 1
)

cd ..

echo.
echo Build complete!
echo DLL location: natives\Windows\chineseime_native.dll
echo.
echo Copy the DLL to your Minecraft mods folder or add to java.library.path
echo.

REM 检查DLL是否生成
if exist "natives\Windows\chineseime_native.dll" (
    echo SUCCESS: chineseime_native.dll created successfully.
) else (
    echo WARNING: DLL not found in expected location.
    echo Searching...
    dir /s /b chineseime_native.dll 2>nul
)

pause
