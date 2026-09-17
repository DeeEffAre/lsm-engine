cmake_minimum_required(VERSION 3.25...3.30)

# ---------------------------------------------------------------------
# vcpkg toolchain fallback
# CMakePresets.json zaten toolchain veriyor; bu kısım presetsiz kullanım için.
# project() çağrılmadan önce ayarlanmalı.
# ---------------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(_myproject_vcpkg_root "$ENV{VCPKG_ROOT}")
  if(NOT _myproject_vcpkg_root)
    set(_myproject_vcpkg_root "$ENV{HOME}/vcpkg")
  endif()

  if(EXISTS "${_myproject_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "${_myproject_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain" FORCE)
  else()
    message(WARNING "vcpkg toolchain not found. Set VCPKG_ROOT or CMAKE_TOOLCHAIN_FILE.")
  endif()
endif()

project(MyProject VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------
# Project options
# ---------------------------------------------------------------------
set(MYPROJECT_CXX_STANDARD 23 CACHE STRING "C++ standard: 17, 20, 23")

option(MYPROJECT_BUILD_TESTS "Build tests" ON)
option(MYPROJECT_BUILD_BENCHMARKS "Build benchmarks" ON)
option(MYPROJECT_ENABLE_PCH "Enable precompiled headers" ON)
option(MYPROJECT_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
option(MYPROJECT_ENABLE_LTO "Enable IPO/LTO for optimized configs" OFF)
option(MYPROJECT_ENABLE_SANITIZERS "Enable sanitizers" OFF)
option(MYPROJECT_ENABLE_COVERAGE "Enable coverage" OFF)
option(MYPROJECT_ENABLE_CCACHE "Enable ccache/sccache" ON)
option(MYPROJECT_ENABLE_CLANG_TIDY "Run clang-tidy during build" OFF)
option(MYPROJECT_ENABLE_INCLUDE_WHAT_YOU_USE "Run include-what-you-use during build" OFF)
option(MYPROJECT_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(MYPROJECT_ENABLE_PROFILING "Enable flags for profiling with tools like perf" OFF)

set(MYPROJECT_SANITIZERS "address,undefined" CACHE STRING
     "Sanitizers: address,undefined,thread,memory,leak")
set(MYPROJECT_LINKER "" CACHE STRING "Linker override: lld, mold, gold, bfd")

# ---------------------------------------------------------------------
# Basic CMake settings
# ---------------------------------------------------------------------
set(CMAKE_CXX_STANDARD ${MYPROJECT_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Default build type for single-config generators
get_property(_myproject_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _myproject_is_multi AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Convenience: symlink compile_commands.json into source root for clangd.
if(NOT CMAKE_HOST_WIN32 AND CMAKE_EXPORT_COMPILE_COMMANDS)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
  )
endif()

# ---------------------------------------------------------------------
# ccache / sccache
# ---------------------------------------------------------------------
if(MYPROJECT_ENABLE_CCACHE)
  find_program(MYPROJECT_CCACHE ccache)
  find_program(MYPROJECT_SCCACHE sccache)

  if(MYPROJECT_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ${MYPROJECT_CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${MYPROJECT_CCACHE})
  elseif(MYPROJECT_SCCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ${MYPROJECT_SCCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${MYPROJECT_SCCACHE})
  endif()
endif()

# ---------------------------------------------------------------------
# clang-tidy / include-what-you-use
# ---------------------------------------------------------------------
if(MYPROJECT_ENABLE_CLANG_TIDY)
  find_program(MYPROJECT_CLANG_TIDY clang-tidy)
  if(MYPROJECT_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY ${MYPROJECT_CLANG_TIDY})
  else()
    message(WARNING "clang-tidy not found")
  endif()
endif()

if(MYPROJECT_ENABLE_INCLUDE_WHAT_YOU_USE)
  find_program(MYPROJECT_IWYU include-what-you-use)
  if(MYPROJECT_IWYU)
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${MYPROJECT_IWYU})
  else()
    message(WARNING "include-what-you-use not found")
  endif()
endif()

if(MYPROJECT_ENABLE_COVERAGE AND MYPROJECT_ENABLE_SANITIZERS)
  message(WARNING "Coverage + sanitizers together can produce misleading results.")
endif()

# ---------------------------------------------------------------------
# Common interface target for compiler options
# ---------------------------------------------------------------------
add_library(myproject_options INTERFACE)

target_compile_features(myproject_options INTERFACE cxx_std_${MYPROJECT_CXX_STANDARD})

if(MSVC)
  target_compile_options(myproject_options INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/W4>
    $<$<COMPILE_LANGUAGE:CXX>:/permissive->
  )

  if(MYPROJECT_WARNINGS_AS_ERRORS)
    target_compile_options(myproject_options INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:/WX>
    )
  endif()
else()
  target_compile_options(myproject_options INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
    $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
    $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
    $<$<COMPILE_LANGUAGE:CXX>:-Wcast-align>
    $<$<COMPILE_LANGUAGE:CXX>:-Wunused>
    $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
    $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wsign-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wnull-dereference>
    $<$<COMPILE_LANGUAGE:CXX>:-Wdouble-promotion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wformat=2>
  )

  if(MYPROJECT_WARNINGS_AS_ERRORS)
    target_compile_options(myproject_options INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:-Werror>
    )
  endif()
endif()

if(MYPROJECT_ENABLE_PROFILING AND NOT MSVC)
  target_compile_options(myproject_options INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-optimize-sibling-calls>
    $<$<COMPILE_LANGUAGE:CXX>:-g3>
  )
endif()

# ---------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------
if(MYPROJECT_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(myproject_options INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address>
    )
    target_link_options(myproject_options INTERFACE
      /INCREMENTAL:NO
    )
  else()
    target_compile_options(myproject_options INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:-g>
      $<$<COMPILE_LANGUAGE:CXX>:-O1>
      $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
      $<$<COMPILE_LANGUAGE:CXX>:-fno-optimize-sibling-calls>
      $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=${MYPROJECT_SANITIZERS}>
    )

    target_link_options(myproject_options INTERFACE
      -fsanitize=${MYPROJECT_SANITIZERS}
    )
  endif()
endif()

# ---------------------------------------------------------------------
# Coverage
# ---------------------------------------------------------------------
if(MYPROJECT_ENABLE_COVERAGE)
  if(NOT MSVC)
    target_compile_options(myproject_options INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:--coverage>
      $<$<COMPILE_LANGUAGE:CXX>:-O0>
      $<$<COMPILE_LANGUAGE:CXX>:-g>
    )

    target_link_options(myproject_options INTERFACE
      --coverage
    )
  endif()
endif()

# ---------------------------------------------------------------------
# Linker selection
# ---------------------------------------------------------------------
if(MYPROJECT_LINKER AND NOT MSVC)
  target_link_options(myproject_options INTERFACE
    -fuse-ld=${MYPROJECT_LINKER}
  )
endif()

# ---------------------------------------------------------------------
# LTO / IPO
# ---------------------------------------------------------------------
if(MYPROJECT_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _myproject_ipo_supported
                      OUTPUT _myproject_ipo_error
                      LANGUAGES C CXX)

  if(_myproject_ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
  else()
    message(WARNING "IPO/LTO not supported: ${_myproject_ipo_error}")
  endif()
endif()

# ---------------------------------------------------------------------
# Helper function for targets
# ---------------------------------------------------------------------
function(myproject_target_setup target)
  target_link_libraries(${target} PUBLIC myproject_options)
  target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/include)

  if(MYPROJECT_ENABLE_PCH)
    target_precompile_headers(${target} PRIVATE
      ${PROJECT_SOURCE_DIR}/cmake/pch.h
    )
  endif()

  if(MYPROJECT_ENABLE_UNITY_BUILD)
    set_target_properties(${target} PROPERTIES
      UNITY_BUILD ON
      UNITY_BUILD_BATCH_SIZE 8
    )
  endif()
endfunction()

# ---------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------
add_subdirectory(src)

if(MYPROJECT_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()

if(MYPROJECT_BUILD_BENCHMARKS)
  add_subdirectory(benchmark)
endif()
