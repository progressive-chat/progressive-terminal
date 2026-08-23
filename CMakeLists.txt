cmake_minimum_required(VERSION 3.16)
project(progressive-terminal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# When OFF, only the static curl-wrapper (render/register/session/input/sync)
# is compiled. The interactive TUI (src/tui.cpp) is gated out so the binary
# builds fast and stays tiny. Turn it ON for the full experience.
option(BUILD_TUI "Build the interactive TUI" ON)

find_package(CURL REQUIRED)

add_executable(progressive-terminal
    src/main.cpp
    src/http.cpp
    src/json.cpp
    src/proto.cpp
    src/render.cpp
    src/store.cpp
    src/discover.cpp
)

if(BUILD_TUI)
    target_sources(progressive-terminal PRIVATE src/tui.cpp)
    target_compile_definitions(progressive-terminal PRIVATE PROGTERM_TUI=1)
endif()

target_include_directories(progressive-terminal PRIVATE src)
target_link_libraries(progressive-terminal PRIVATE CURL::libcurl)

install(TARGETS progressive-terminal RUNTIME DESTINATION bin)
