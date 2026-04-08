cmake_minimum_required(VERSION 3.14)

project(SYNTHRLN
    VERSION 1.0.0
    DESCRIPTION "Multi-Platform BBS-to-TCP Bridge"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================================
# Source files
# ============================================================================

set(SRC_COMMON
    src/main.c
    src/config.c
    src/dropfile.c
)

# Platform-specific sources
if(WIN32)
    set(SRC_PLATFORM
        src/bbs_posix.c
        src/net_posix.c
    )
elseif(UNIX AND NOT APPLE)
    set(SRC_PLATFORM
        src/bbs_posix.c
        src/net_posix.c
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "DOS")
    # DOS/OpenWatcom cross-compilation
    # mTCP sources expected in vendor/mtcp/
    set(SRC_PLATFORM
        src/bbs_fossil.c
        src/bbs_local.c
        src/net_mtcp.c
    )
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# ============================================================================
# Executable
# ============================================================================

add_executable(synthrln ${SRC_COMMON} ${SRC_PLATFORM})

target_include_directories(synthrln PRIVATE src)

# ============================================================================
# Compiler flags
# ============================================================================

target_compile_options(synthrln PRIVATE
    $<$<C_COMPILER_ID:GNU>:-Wall -Wextra -Wno-unused-parameter>
    $<$<C_COMPILER_ID:Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<C_COMPILER_ID:MSVC>:/W3>
)

# ============================================================================
# Platform-specific linkage
# ============================================================================

if(WIN32)
    target_link_libraries(synthrln PRIVATE ws2_32)
    target_compile_definitions(synthrln PRIVATE WIN32 _WIN32)
elseif(UNIX)
    target_compile_definitions(synthrln PRIVATE __linux__)
endif()

# ============================================================================
# DOS / mTCP (OpenWatcom cross-compile)
# ============================================================================

if(CMAKE_SYSTEM_NAME STREQUAL "DOS")
    target_compile_definitions(synthrln PRIVATE __MSDOS__ __WATCOMC__)
    # Point to mTCP library if present
    if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/mtcp/lib/mtcplib.lib")
        target_include_directories(synthrln PRIVATE vendor/mtcp/include)
        target_link_libraries(synthrln PRIVATE
            ${CMAKE_SOURCE_DIR}/vendor/mtcp/lib/mtcplib.lib
        )
    else()
        message(WARNING
            "mTCP library not found at vendor/mtcp/. "
            "DOS build will compile but not link. "
            "See docs/BUILDING_DOS.md for setup instructions."
        )
    endif()
endif()

# ============================================================================
# Install
# ============================================================================

install(TARGETS synthrln
    RUNTIME DESTINATION bin
)

install(FILES
    synthrln.cfg.example
    DESTINATION share/synthrln
    RENAME synthrln.cfg.example
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "SYNTHRLN ${PROJECT_VERSION}")
message(STATUS "  Platform : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Build dir: ${CMAKE_BINARY_DIR}")
