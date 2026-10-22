cmake_minimum_required(VERSION 3.24)
project(IMPCS2 VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "IMPCS2 currently supports Windows only.")
endif()

add_executable(IMPCS2 src/main.cpp)
target_compile_features(IMPCS2 PRIVATE cxx_std_17)
target_compile_definitions(IMPCS2 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(IMPCS2 PRIVATE Xinput9_1_0)

if(MSVC)
  target_compile_options(IMPCS2 PRIVATE /W4 /permissive- /EHsc)
endif()
