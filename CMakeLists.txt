cmake_minimum_required(VERSION 3.16)
project(nanomatch CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Toggle: link the SPSC-ring + std::thread async logger (Brick 12) into the
# demo binary. Default OFF on Windows because std::thread in unsigned MinGW
# binaries trips SmartScreen; flip ON in WSL2/Linux or after signing.
option(NANOMATCH_USE_ASYNC_LOGGER "Use AsyncTradeLogger in demo binary" OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /O2 /DNDEBUG /arch:AVX2)
else()
    add_compile_options(-Wall -Wextra -O3 -DNDEBUG -march=native -fno-omit-frame-pointer)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# --- Core library ---------------------------------------------------------
add_library(nanomatch_core
    src/order_book.cpp
    src/order_book_naive.cpp
    src/itch_parser.cpp
    src/logger.cpp
    src/file_map.cpp
)
target_include_directories(nanomatch_core PUBLIC include)

# --- Main demo binary -----------------------------------------------------
add_executable(nanomatch src/main.cpp)
target_link_libraries(nanomatch PRIVATE nanomatch_core)
if(NANOMATCH_USE_ASYNC_LOGGER)
    target_compile_definitions(nanomatch PRIVATE NANOMATCH_USE_ASYNC_LOGGER=1)
endif()

# --- Benchmarks (no external deps; uses internal histogram) ---------------
add_executable(nanomatch_bench benchmarks/bench_match.cpp)
target_link_libraries(nanomatch_bench PRIVATE nanomatch_core)

# --- Tests (self-contained, no gtest dependency) --------------------------
enable_testing()
add_executable(nanomatch_tests tests/test_order_book.cpp)
target_link_libraries(nanomatch_tests PRIVATE nanomatch_core)
add_test(NAME nanomatch_tests COMMAND nanomatch_tests)
