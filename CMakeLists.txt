cmake_minimum_required(VERSION 3.14)
include_guard(DIRECTORY)

project(
  slimfmt
  VERSION 0.0.7
  LANGUAGES CXX
)

option(SLIMFMT_TESTING "Enable tests." OFF)
option(SLIMFMT_FORCE_ASSERT "Keep internal assertions on in release." OFF)
option(SLIMFMT_STDERR_ASSERT "Print assertions to stderr instead of aborting." OFF)
# option(SLIMFMT_FORMAT_CHECKS "Enable compile time format checks." OFF)

message(STATUS "[slimfmt] testing: ${SLIMFMT_TESTING}")
message(STATUS "[slimfmt] force-assert: ${SLIMFMT_FORCE_ASSERT}")
message(STATUS "[slimfmt] stderr-assert: ${SLIMFMT_STDERR_ASSERT}")

add_library(slimfmt STATIC src/Slimfmt.cpp)
add_library(slimfmt::slimfmt ALIAS slimfmt)
target_include_directories(slimfmt PUBLIC src)
target_compile_features(slimfmt PUBLIC cxx_std_20)
target_compile_definitions(slimfmt PRIVATE
  "SLIMFMT_FORCE_ASSERT=$<BOOL:${SLIMFMT_FORCE_ASSERT}>"
  "SLIMFMT_STDERR_ASSERT=$<BOOL:${SLIMFMT_STDERR_ASSERT}>"
)

if(SLIMFMT_TESTING)
  add_executable(driver Driver.cpp)
  target_link_libraries(driver PRIVATE slimfmt::slimfmt)
  target_link_options(driver PUBLIC -ggdb)
endif()