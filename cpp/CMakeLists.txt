cmake_minimum_required(VERSION 3.20)
project(MidnightLauncherCpp LANGUAGES CXX)

# иконка приложения встраивается в exe (src/app.rc.in + копия app.ico в build)
enable_language(RC)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../midnight.ico"
               "${CMAKE_CURRENT_BINARY_DIR}/app.ico" COPYONLY)
configure_file(src/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MidnightL WIN32 src/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/app.rc")

target_compile_definitions(MidnightL PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)

target_link_libraries(MidnightL PRIVATE d2d1 dwrite dwmapi gdi32 ole32 uuid winhttp windowscodecs shlwapi shell32 comdlg32 bcrypt)

if (MINGW)
    target_link_options(MidnightL PRIVATE
        -municode -mwindows -static -static-libgcc -static-libstdc++)
endif()
