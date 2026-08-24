cmake_minimum_required(VERSION 3.16)
project(progressive-terminal VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)

add_executable(progterm-lite
    src/main.cpp
    src/net.cpp
    src/store.cpp
)

target_include_directories(progterm-lite PRIVATE src)
target_link_libraries(progterm-lite PRIVATE CURL::libcurl)

install(TARGETS progterm-lite RUNTIME DESTINATION bin)
