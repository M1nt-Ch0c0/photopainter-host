if(DEFINED ENV{PHOTOFRAME_COMPONENT_DIR} AND NOT "$ENV{PHOTOFRAME_COMPONENT_DIR}" STREQUAL "")
    get_filename_component(PHOTOFRAME_COMPONENT_DIR "$ENV{PHOTOFRAME_COMPONENT_DIR}" ABSOLUTE)
else()
    # CMAKE_SOURCE_DIR points at a temporary requirements project while ESP-IDF
    # discovers component dependencies.  Anchor the sibling checkout to this
    # file so both that pass and the real configure resolve the same path.
    get_filename_component(PHOTOFRAME_COMPONENT_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../photoframe" ABSOLUTE)
endif()

if(NOT EXISTS "${PHOTOFRAME_COMPONENT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "photoframe component project not found at ${PHOTOFRAME_COMPONENT_DIR}; "
        "clone M1nt-Ch0c0/photoframe beside this repository or set PHOTOFRAME_COMPONENT_DIR")
endif()

idf_component_register(
    SRCS
        "app_main.c"
        "host_config.c"
        "photoframe_plugin.c"
        "push_server.c"
        "wifi_station.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_event
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        freertos
        heap
        nvs_flash
)

# elf_loader's project include supplies this function. It builds the sibling
# project as photoframe.app.elf, embeds it, and generates only the host symbols
# that the payload actually imports.
elf_embed_binary(photoframe "${PHOTOFRAME_COMPONENT_DIR}" photoframe)
