idf_component_register(
  SRCS "app_main.cpp" "covers.cpp" "diag.cpp"
  INCLUDE_DIRS "."
  PRIV_REQUIRES fernotron shutter esp_matter esp_matter_console esp_driver_gpio esp_timer nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
