idf_component_register(
  SRCS "app_main.cpp" "cli.cpp" "covers.cpp" "diag.cpp" "panel_hw.cpp" "rf_tx.cpp" "settings.cpp" "setup_code.cpp"
  INCLUDE_DIRS "."
  PRIV_REQUIRES config fernotron pairing panel shutter led_strip bootloader_support esp_hw_support esp_matter esp_matter_console esp_driver_gpio esp_driver_rmt esp_timer nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
