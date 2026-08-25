@echo on
cd native
cmake -B build
cmake --build build --config Release
cd ..
.\gradlew.bat clean build 2>&1

echo.
echo --------------------------------------
echo 编译完成！
echo --------------------------------------
pause