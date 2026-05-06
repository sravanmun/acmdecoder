cmake_minimum_required(VERSION 3.14)
project(decoderlib LANGUAGES C CXX)

# ----- C++ setup -------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ----- pybind11 (only find if not already found by parent) -------------------
if(NOT TARGET pybind11::module)
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(pybind11 REQUIRED CONFIG)
endif()

# ----- Compression libs: try system first, fall back to FetchContent ---------
# Set ACMDECODER_FETCH_DEPS=1 in the environment to skip the system-libs
# lookup and always build from source (used in CI for self-contained wheels).
if(NOT DEFINED ENV{ACMDECODER_FETCH_DEPS})
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(ZSTD QUIET IMPORTED_TARGET libzstd)
    pkg_check_modules(BZ2  QUIET IMPORTED_TARGET bzip2)
  endif()
endif()

include(FetchContent)

# zstd
if(ZSTD_FOUND)
  add_library(zstd_dep INTERFACE)
  target_link_libraries(zstd_dep INTERFACE PkgConfig::ZSTD)
else()
  message(STATUS "libzstd not found on system; building from source via FetchContent")
  set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_STATIC   ON  CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG        v1.5.6
    SOURCE_SUBDIR  build/cmake
  )
  FetchContent_MakeAvailable(zstd)
  add_library(zstd_dep INTERFACE)
  target_link_libraries(zstd_dep INTERFACE libzstd_static)
  target_include_directories(zstd_dep INTERFACE ${zstd_SOURCE_DIR}/lib)
endif()

# bzip2 — upstream has no CMakeLists, so we fetch the tarball and build it ourselves
if(BZ2_FOUND)
  add_library(bz2_dep INTERFACE)
  target_link_libraries(bz2_dep INTERFACE PkgConfig::BZ2)
else()
  message(STATUS "libbz2 not found on system; building from source via FetchContent")
  FetchContent_Declare(bzip2_src
    URL      https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
  )
  FetchContent_GetProperties(bzip2_src)
  if(NOT bzip2_src_POPULATED)
    FetchContent_Populate(bzip2_src)
  endif()
  add_library(bzip2_static STATIC
    ${bzip2_src_SOURCE_DIR}/blocksort.c
    ${bzip2_src_SOURCE_DIR}/huffman.c
    ${bzip2_src_SOURCE_DIR}/crctable.c
    ${bzip2_src_SOURCE_DIR}/randtable.c
    ${bzip2_src_SOURCE_DIR}/compress.c
    ${bzip2_src_SOURCE_DIR}/decompress.c
    ${bzip2_src_SOURCE_DIR}/bzlib.c
  )
  set_target_properties(bzip2_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(bzip2_static PUBLIC ${bzip2_src_SOURCE_DIR})
  add_library(bz2_dep INTERFACE)
  target_link_libraries(bz2_dep INTERFACE bzip2_static)
endif()

# ----- Core C++ library (OBJECT) ---------------------------------------------
set(PROJ_SOURCES
  src/decoder.cpp
  src/logger.cpp
)

add_library(decoder_core OBJECT ${PROJ_SOURCES})
target_include_directories(decoder_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(decoder_core PUBLIC zstd_dep bz2_dep)

# ----- Python bindings -------------------------------------------------------
pybind11_add_module(decoder
  bindings/decoder.cpp
  $<TARGET_OBJECTS:decoder_core>
)

target_include_directories(decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(decoder PRIVATE decoder_core zstd_dep bz2_dep)

# macOS rpath so the loader can find deps next to the module
if(APPLE)
  set_target_properties(decoder PROPERTIES
    INSTALL_RPATH "@loader_path/;@rpath")
endif()

# ----- Install ---------------------------------------------------------------
install(TARGETS decoder LIBRARY DESTINATION acmdecoder)
