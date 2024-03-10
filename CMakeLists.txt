cmake_minimum_required(VERSION 3.14)
include_guard(DIRECTORY)

project(
  slimfmt
  VERSION 0.0.2
  LANGUAGES CXX
)

option(SLIMFMT_TESTING "Enable tests." OFF)

add_library(slimfmt STATIC src/Slimfmt.cpp)
add_library(slimfmt::slimfmt ALIAS slimfmt)
target_include_directories(slimfmt PUBLIC src)
target_compile_features(slimfmt PUBLIC cxx_std_17)

if(SLIMFMT_TESTING)
  add_executable(driver Driver.cpp)
  target_link_libraries(driver PRIVATE slimfmt::slimfmt)
  target_link_options(driver PUBLIC -ggdb)
endif()