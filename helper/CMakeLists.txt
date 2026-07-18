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

# NeuralAmpModelerCore (MIT) — compiled directly; its CMake only builds tools.
FetchContent_Declare(
  namcore
  GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG        v0.5.4
  SOURCE_SUBDIR  cmake-download-only  # no CMakeLists there: fetch without add_subdirectory
)
FetchContent_MakeAvailable(namcore)

file(GLOB NAM_SOURCES
  "${namcore_SOURCE_DIR}/NAM/*.cpp"
  "${namcore_SOURCE_DIR}/NAM/wavenet/*.cpp"
)
add_library(namcore STATIC ${NAM_SOURCES})
target_include_directories(namcore PUBLIC
  "${namcore_SOURCE_DIR}"
  "${namcore_SOURCE_DIR}/NAM"
  "${namcore_SOURCE_DIR}/Dependencies/eigen"
  "${namcore_SOURCE_DIR}/Dependencies/nlohmann"
)
target_compile_features(namcore PUBLIC cxx_std_20)
target_compile_definitions(namcore PUBLIC
  NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST NOMINMAX WIN32_LEAN_AND_MEAN)

# FFTConvolver (MIT) — partitioned convolution for cabinet IRs.
FetchContent_Declare(
  fftconvolver
  GIT_REPOSITORY https://github.com/HiFi-LoFi/FFTConvolver.git
  GIT_TAG        f2cdeb04c42141d2caec19ca4f137398b2a76b85
)
FetchContent_MakeAvailable(fftconvolver)
file(GLOB FFTCONV_SOURCES "${fftconvolver_SOURCE_DIR}/*.cpp")
add_library(fftconvolver STATIC ${FFTCONV_SOURCES})
target_include_directories(fftconvolver PUBLIC "${fftconvolver_SOURCE_DIR}")

# dr_wav (public domain) — WAV loading for IRs.
FetchContent_Declare(
  drlibs
  GIT_REPOSITORY https://github.com/mackron/dr_libs.git
  GIT_TAG        6d78776c2c05358e351e3c67878a8b681c76c5d1
)
FetchContent_MakeAvailable(drlibs)

add_executable(p1a src/main.cpp)
target_link_libraries(p1a PRIVATE portaudio)

add_executable(p1b src/amp.cpp)
target_include_directories(p1b PRIVATE "${drlibs_SOURCE_DIR}")
# WHOLE_ARCHIVE: NAM's config parsers self-register via static initializers,
# which the linker would otherwise drop from a static lib.
target_link_libraries(p1b PRIVATE portaudio "$<LINK_LIBRARY:WHOLE_ARCHIVE,namcore>" fftconvolver)


