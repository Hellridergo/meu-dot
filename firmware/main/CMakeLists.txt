idf_component_register(SRCS "main.c" "link_ble.c" "voice.c" "settings.c" "battery.c" "../../interface/dot_ui.c"
    INCLUDE_DIRS "." "../../interface" REQUIRES esp_driver_gpio bt nvs_flash esp_timer esp_driver_i2s esp_driver_i2c espressif__esp_codec_dev)
