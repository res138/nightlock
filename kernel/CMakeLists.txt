cmake_minimum_required(VERSION 3.14)

project(NightlockKernel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(kernel
kernel.cpp
src/Manager.cpp
)
