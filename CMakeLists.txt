cmake_minimum_required(VERSION 3.16)

project(audd
    VERSION 1.5.5
    LANGUAGES C
    DESCRIPTION "Official C SDK for the AudD music recognition API"
)

option(AUDD_BUILD_EXAMPLES "Build example programs" OFF)
option(AUDD_BUILD_TESTS    "Build unit tests"       OFF)
option(AUDD_BUILD_SHARED   "Build shared library"   ON)
option(AUDD_BUILD_STATIC   "Build static library"   ON)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# --- Dependencies ----------------------------------------------------------
# Prefer pkg-config for libcurl (most flexible across distros), fall back to
# CMake's bundled FindCURL.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CURL libcurl)
endif()
if(NOT CURL_FOUND)
    find_package(CURL REQUIRED)
endif()

# --- Sources ---------------------------------------------------------------
set(AUDD_VENDOR_SOURCES
    vendor/cJSON/cJSON.c
)

set(AUDD_SOURCES
    src/audd_alloc.c
    src/audd_error.c
    src/audd_md5.c
    src/audd_url.c
    src/audd_json.c
    src/audd_http.c
    src/audd_client.c
    src/audd_recognition.c
    src/audd_enterprise.c
    src/audd_callback.c
    src/audd_streams.c
    src/audd_longpoll.c
    src/audd_custom_catalog.c
    src/audd_advanced.c
    ${AUDD_VENDOR_SOURCES}
)

set(AUDD_PUBLIC_HEADERS
    include/audd.h
    include/audd_version.h
)

# --- Object library, then shared + static derive from it ------------------
add_library(audd_objlib OBJECT ${AUDD_SOURCES})
target_include_directories(audd_objlib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cJSON
)
target_compile_features(audd_objlib PRIVATE c_std_99)
if(CURL_INCLUDE_DIRS)
    target_include_directories(audd_objlib PRIVATE ${CURL_INCLUDE_DIRS})
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(audd_objlib PRIVATE -Wall -Wextra -Wno-unused-parameter -fPIC)
endif()
set_target_properties(audd_objlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Public targets that add interface deps (libcurl) without duplicating source.
if(AUDD_BUILD_SHARED)
    add_library(audd SHARED $<TARGET_OBJECTS:audd_objlib>)
    target_include_directories(audd
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    if(CURL_LIBRARIES)
        target_link_libraries(audd PRIVATE ${CURL_LIBRARIES})
    elseif(TARGET CURL::libcurl)
        target_link_libraries(audd PRIVATE CURL::libcurl)
    endif()
    if(CURL_LDFLAGS_OTHER)
        target_link_options(audd PRIVATE ${CURL_LDFLAGS_OTHER})
    endif()
    set_target_properties(audd PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME audd
    )
endif()

if(AUDD_BUILD_STATIC)
    add_library(audd_static STATIC $<TARGET_OBJECTS:audd_objlib>)
    target_include_directories(audd_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    if(CURL_LIBRARIES)
        target_link_libraries(audd_static PUBLIC ${CURL_LIBRARIES})
    elseif(TARGET CURL::libcurl)
        target_link_libraries(audd_static PUBLIC CURL::libcurl)
    endif()
    set_target_properties(audd_static PROPERTIES OUTPUT_NAME audd)
endif()

# --- Examples --------------------------------------------------------------
if(AUDD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Tests -----------------------------------------------------------------
if(AUDD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Install rules ---------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(AUDD_BUILD_SHARED)
    install(TARGETS audd
        EXPORT auddTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(AUDD_BUILD_STATIC)
    install(TARGETS audd_static
        EXPORT auddTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
install(FILES ${AUDD_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# pkg-config file for downstream consumers.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/audd.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/audd.pc"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/audd.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
