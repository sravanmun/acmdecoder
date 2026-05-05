cmake_minimum_required(VERSION 3.10)
project(decoderlib LANGUAGES CXX)

# ----- C++ setup -------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ----- pybind11 (only find if not already found by parent) -------------------
if(NOT TARGET pybind11::module)
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(pybind11 REQUIRED CONFIG)
endif()

# ----- Find compression libraries ---------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(BZ2  REQUIRED IMPORTED_TARGET bzip2)

# ----- Core C++ library (OBJECT) ---------------------------------------------
set(PROJ_SOURCES
  src/decoder.cpp
  src/logger.cpp
)

add_library(decoder_core OBJECT ${PROJ_SOURCES})
target_include_directories(decoder_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(decoder_core PUBLIC PkgConfig::ZSTD PkgConfig::BZ2)

# ----- Python bindings -------------------------------------------------------
pybind11_add_module(decoder
  bindings/decoder.cpp
  $<TARGET_OBJECTS:decoder_core>
)

target_include_directories(decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(decoder PRIVATE decoder_core PkgConfig::ZSTD PkgConfig::BZ2)

# macOS rpath so the loader can find deps next to the module
if(APPLE)
  set_target_properties(decoder PROPERTIES
    INSTALL_RPATH "@loader_path/;@rpath")
endif()

# ----- Install ---------------------------------------------------------------
install(TARGETS decoder LIBRARY DESTINATION acmdecoder)
