# Pure C++17, no ESP-IDF headers: also built on the host by test/CMakeLists.txt.
idf_component_register(
  SRCS "src/config/house.cpp" "src/config/command.cpp"
  INCLUDE_DIRS "src")
