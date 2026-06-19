# CMakeLists.txt — build.ps1'e alternatif (MSVC, Windows).
# Yapılandırma:
#   cmake -S . -B build -G "Visual Studio 18 2026" -A x64
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.20)
project(ITHelper LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "ITHelper yalnızca MSVC ile derlenir (saf Win32).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statik CRT (bağımsız EXE — harici redist yok).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(ITHelper WIN32
    main.cpp
    Shell.cpp
    AdQuery.cpp
    AdQueryTool.cpp
    ITHelper.rc
)

target_compile_options(ITHelper PRIVATE
    /W4 /utf-8 /permissive-
    /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00
    $<$<CONFIG:Release>:/O1 /Os /GL>
)

target_link_options(ITHelper PRIVATE
    /MANIFEST:EMBED
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    /MAP:ITHelper.map
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
)

target_link_libraries(ITHelper PRIVATE
    activeds adsiid comctl32 ole32 oleaut32 user32 gdi32 advapi32 shell32
)
