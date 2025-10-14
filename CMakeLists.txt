cmake_minimum_required(VERSION 3.15)
project(SpellForge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Clang + libc++ (macOS/Linux)
if(APPLE OR (UNIX AND NOT WIN32))
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "This project requires Clang/Clang++. Re-configure with: "
            "-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++")
    endif()

    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)

    if(UNIX AND NOT APPLE)
        set(FORGE_LIBCXX_EXTRA_LIBS c++ c++abi)
    endif()
endif()

# Sanitizers
if (CMAKE_COMPILER_IS_GNUCXX OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Output dirs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------- Your sources ----------
file(GLOB_RECURSE CUSTOM_SOURCES
    src/*.cpp
    src/*.c
)
add_executable(SpellForge ${CUSTOM_SOURCES})

# ---------- Headers ----------
# (Your own project headers)
target_include_directories(SpellForge PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/include
)

# ---------- Imported static libs ----------
# Raylib
add_library(raylib STATIC IMPORTED GLOBAL)
set_target_properties(raylib PROPERTIES
    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/lib/raylib/build/raylib/libraylib.a"
    INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/lib/raylib/src"
)

# Box2D
add_library(box2d STATIC IMPORTED GLOBAL)
set_target_properties(box2d PROPERTIES
    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/lib/box2d/build/src/libbox2d.a"
    INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/lib/box2d/include"
)

# Let our exe consume their include dirs automatically
target_link_libraries(SpellForge PRIVATE raylib box2d)

# ---------- Platform system libs (Raylib expects these) ----------
set(PLATFORM_LIBS)
if(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(COREVIDEO_LIBRARY CoreVideo)
    find_library(OPENGL_LIBRARY OpenGL)
    list(APPEND PLATFORM_LIBS
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
        ${COREGRAPHICS_LIBRARY}
        ${COREVIDEO_LIBRARY}
        ${OPENGL_LIBRARY}
    )
elseif(UNIX) # Linux
    # Raylib usually needs these on Linux when linking statically
    list(APPEND PLATFORM_LIBS
        GL
        X11 Xrandr Xi Xinerama Xcursor
        pthread dl m
    )
elseif(WIN32)
    # Typical Raylib deps on Windows
    list(APPEND PLATFORM_LIBS
        opengl32
        winmm gdi32 user32 shell32
    )
endif()

# ---------- Final link ----------
# Order matters for static linking; keep raylib & box2d before platform libs.
target_link_libraries(SpellForge PRIVATE
    raylib
    box2d
    ${PLATFORM_LIBS}
    ${FORGE_LIBCXX_EXTRA_LIBS}
)
