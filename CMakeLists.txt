cmake_minimum_required(VERSION 3.24)
project(foldproof LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(tree_sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter
  GIT_TAG v0.25.9
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR do-not-build)
FetchContent_Declare(tree_sitter_cpp
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp
  GIT_TAG v0.23.4
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR do-not-build)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tree_sitter tree_sitter_cpp googletest)

add_library(ts_runtime STATIC
  ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
  ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
  ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c)
target_include_directories(ts_runtime
  PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include
  PRIVATE ${tree_sitter_SOURCE_DIR}/lib/src ${tree_sitter_cpp_SOURCE_DIR}/src)
set_target_properties(ts_runtime PROPERTIES C_STANDARD 11)
target_compile_definitions(ts_runtime PRIVATE _POSIX_C_SOURCE=200112L _DEFAULT_SOURCE)

set(LLVM_PREFIX "/opt/homebrew/opt/llvm" CACHE PATH "LLVM install providing libclang")
unset(LIBCLANG CACHE)
find_library(LIBCLANG clang PATHS ${LLVM_PREFIX}/lib NO_DEFAULT_PATH REQUIRED)
execute_process(COMMAND ${LLVM_PREFIX}/bin/clang -print-resource-dir
  OUTPUT_VARIABLE CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)

add_library(foldproof_warnings INTERFACE)
target_compile_options(foldproof_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

add_library(foldproof_core STATIC
  engine/src/version.cpp
  engine/src/clang_tu.cpp
  engine/src/format_check.cpp
  engine/src/rename_check.cpp
  engine/src/move_check.cpp)
target_include_directories(foldproof_core PUBLIC engine/src)
target_compile_definitions(foldproof_core PRIVATE FOLDPROOF_CLANG_RESOURCE_DIR="${CLANG_RESOURCE_DIR}")
target_include_directories(foldproof_core SYSTEM PUBLIC ${LLVM_PREFIX}/include)
target_link_libraries(foldproof_core PUBLIC ${LIBCLANG} ts_runtime PRIVATE foldproof_warnings)

add_executable(foldproof engine/cli/main.cpp)
target_link_libraries(foldproof PRIVATE foldproof_core foldproof_warnings)

enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS engine/tests/*_test.cpp)
add_executable(foldproof_tests ${TEST_SOURCES})
target_link_libraries(foldproof_tests PRIVATE foldproof_core foldproof_warnings GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(foldproof_tests DISCOVERY_TIMEOUT 60 DISCOVERY_MODE PRE_TEST)
