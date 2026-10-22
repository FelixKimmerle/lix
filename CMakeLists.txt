cmake_minimum_required(VERSION 3.20)
project(lix CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB_RECURSE LIBLIX_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM LIBLIX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

add_library(liblix STATIC ${LIBLIX_SOURCES})
target_include_directories(liblix PUBLIC src)
target_compile_options(liblix PRIVATE -Wall -Wextra -Wpedantic)

option(LIX_USE_READLINE "Use GNU readline for line editing and history in the REPL" ON)
if(LIX_USE_READLINE)
    find_path(READLINE_INCLUDE_DIR readline/readline.h)
    find_library(READLINE_LIBRARY readline)
    if(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
        target_compile_definitions(liblix PRIVATE LIX_HAVE_READLINE)
        target_include_directories(liblix PRIVATE ${READLINE_INCLUDE_DIR})
        target_link_libraries(liblix PUBLIC ${READLINE_LIBRARY})
        message(STATUS "REPL line editing: readline")
    else()
        message(STATUS "REPL line editing: none (readline not found)")
    endif()
endif()

add_executable(lix src/main.cpp)
target_link_libraries(lix PRIVATE liblix)
target_compile_options(lix PRIVATE -Wall -Wextra -Wpedantic)

option(LIX_SANITIZE "Build with address and undefined behaviour sanitizers" OFF)
if(LIX_SANITIZE)
    foreach(target liblix lix)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME lix-tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py $<TARGET_FILE:lix>)
    add_test(NAME lix-tests-gc-stress
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py $<TARGET_FILE:lix> --gc-stress)
endif()
