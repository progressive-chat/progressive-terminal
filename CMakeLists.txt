cmake_minimum_required(VERSION 3.16)
project(progressive-terminal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)

add_executable(progressive-terminal
    src/main.cpp
    src/proto.cpp
    src/http.cpp
    src/json.cpp
    src/render.cpp
    src/store.cpp
    src/discover.cpp
)


target_include_directories(progressive-terminal PRIVATE src)
target_link_libraries(progressive-terminal PRIVATE CURL::libcurl)

install(TARGETS progressive-terminal RUNTIME DESTINATION bin)
