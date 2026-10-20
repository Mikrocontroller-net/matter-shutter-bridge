# Pure C++17, no ESP-IDF headers: also built on the host by test/CMakeLists.txt.
idf_component_register(
  SRCS "src/shutter/estimator.cpp" "src/shutter/controller.cpp"
  INCLUDE_DIRS "src")
