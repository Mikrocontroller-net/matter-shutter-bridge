idf_component_register(
  SRCS "app_main.cpp" "cli.cpp" "covers.cpp" "diag.cpp" "panel_hw.cpp" "remote.cpp" "rf_tx.cpp" "settings.cpp" "setup_code.cpp"
  INCLUDE_DIRS "."
  PRIV_REQUIRES config fernotron pairing panel remote shutter led_strip app_update bootloader_support console esp_app_format esp_hw_support esp_http_server esp_matter esp_matter_console esp_netif esp_driver_gpio esp_driver_rmt esp_driver_usb_serial_jtag esp_timer nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
