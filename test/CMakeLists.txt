# Host unit tests for the hardware-free components. Uses the Unity copy that
# ships with ESP-IDF, so `source tools/env.sh` first; tools/host_test.sh does
# both steps.
cmake_minimum_required(VERSION 3.16)
project(fernomatter_host_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

if(NOT DEFINED ENV{IDF_PATH})
  message(FATAL_ERROR "IDF_PATH is not set; run: source tools/env.sh")
endif()
set(UNITY_DIR $ENV{IDF_PATH}/components/unity/unity/src)
add_library(unity STATIC ${UNITY_DIR}/unity.c)
target_include_directories(unity PUBLIC ${UNITY_DIR})

set(COMPONENTS ${CMAKE_CURRENT_SOURCE_DIR}/../components)

add_library(fernotron STATIC
  ${COMPONENTS}/fernotron/src/fernotron/message.cpp
  ${COMPONENTS}/fernotron/src/fernotron/encoder.cpp
  ${COMPONENTS}/fernotron/src/fernotron/airtime.cpp
  ${COMPONENTS}/fernotron/src/fernotron/batcher.cpp)
target_include_directories(fernotron PUBLIC ${COMPONENTS}/fernotron/src)

add_library(shutter STATIC
  ${COMPONENTS}/shutter/src/shutter/estimator.cpp
  ${COMPONENTS}/shutter/src/shutter/controller.cpp)
target_include_directories(shutter PUBLIC ${COMPONENTS}/shutter/src)

add_library(config STATIC
  ${COMPONENTS}/config/src/config/house.cpp
  ${COMPONENTS}/config/src/config/command.cpp)
target_include_directories(config PUBLIC ${COMPONENTS}/config/src)

enable_testing()
foreach(name fernotron shutter config)
  add_executable(test_${name} test_${name}/test_main.cpp)
  target_link_libraries(test_${name} PRIVATE ${name} unity)
  add_test(NAME ${name} COMMAND test_${name})
endforeach()

# Extra suites for the fernotron library.
foreach(name batcher)
  add_executable(test_${name} test_${name}/test_main.cpp)
  target_link_libraries(test_${name} PRIVATE fernotron unity)
  add_test(NAME ${name} COMMAND test_${name})
endforeach()
