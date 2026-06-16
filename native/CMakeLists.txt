cmake_minimum_required(VERSION 3.15)
project(ChineseIME_Native VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows DLL
add_library(chineseime_native SHARED
src/ime_bridge.cpp
src/sta_thread.cpp
src/ime_state_manager.cpp
src/jni_callback.cpp
src/tsf_monitor.cpp
src/imm32_monitor.cpp
src/win_event_bridge.cpp
)

# Include directories
target_include_directories(chineseime_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Windows specific settings
if(WIN32)
    target_link_libraries(chineseime_native PRIVATE
        user32
        ole32
        oleaut32
        imm32
    )
    
    target_compile_definitions(chineseime_native PRIVATE
        CHINESEIME_EXPORTS
        UNICODE
        _UNICODE
    )
endif()

# Output to natives folder
set_target_properties(chineseime_native PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../natives
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../natives
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../natives
)