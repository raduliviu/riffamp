cmake_minimum_required(VERSION 3.21)
project(webamp-helper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ASIO SDK (Steinberg, GPLv3/proprietary dual-licensed since late 2025).
# PortAudio's CMake picks it up via ASIOSDK_ROOT_DIR / find module.
FetchContent_Declare(
  asiosdk
  URL https://download.steinberg.net/sdk_downloads/asiosdk_2.3.3_2019-06-14.zip
)
FetchContent_MakeAvailable(asiosdk)
set(ASIOSDK_ROOT_DIR "${asiosdk_SOURCE_DIR}" CACHE PATH "" FORCE)
set(ASIO_SDK_DIR "${asiosdk_SOURCE_DIR}" CACHE PATH "" FORCE)

set(PA_USE_ASIO ON CACHE BOOL "" FORCE)
set(PA_USE_WASAPI ON CACHE BOOL "" FORCE)
set(PA_USE_WDMKS ON CACHE BOOL "" FORCE)
set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  portaudio
  GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(portaudio)

add_executable(p1a src/main.cpp)
target_link_libraries(p1a PRIVATE portaudio)
