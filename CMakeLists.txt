cmake_minimum_required(VERSION 3.20)
project(RIFEOS LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(RIFEOS WIN32
    src/rife_core.h
    src/rife_core.c
    src/app_settings.h
    src/app_settings.c
    src/main.c
    src/rife_app_api.h
    src/app_manifest.h
    src/app_manifest.c
    resources/rifeos.rc
)

target_include_directories(RIFEOS PRIVATE src resources assets)

if(WIN32)
    target_link_libraries(RIFEOS PRIVATE 
        user32 
        gdi32 
        dwmapi 
        winmm 
        shell32
    )
endif()

if(MSVC)
    set_property(TARGET RIFEOS PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(RIFEOS PRIVATE /W4 /utf-8 $<$<CONFIG:Release>:/O2 /fp:fast>)
else()
    target_compile_options(RIFEOS PRIVATE -Wall -Wextra)
endif()