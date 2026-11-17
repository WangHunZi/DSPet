set(asset_dir "${CMAKE_CURRENT_LIST_DIR}/../assets/generated")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${asset_dir}/idle.json")
file(READ "${asset_dir}/idle.json" asset_json)
foreach(field width height fps frame_count bytes_per_frame format sha256)
    string(JSON asset_${field} GET "${asset_json}" ${field})
endforeach()
math(EXPR expected_frame_bytes "${asset_width} * ${asset_height} * 2")
math(EXPR expected_bytes "${asset_frame_count} * ${expected_frame_bytes}")
file(SIZE "${asset_dir}/idle.rgb565" actual_bytes)
file(SHA256 "${asset_dir}/idle.rgb565" actual_sha256)
if(NOT asset_format STREQUAL "RGB565_BE" OR
   NOT asset_width EQUAL 240 OR NOT asset_height EQUAL 240 OR
   NOT asset_frame_count GREATER 0 OR NOT asset_fps GREATER 0 OR asset_fps GREATER 60 OR
   NOT asset_bytes_per_frame EQUAL expected_frame_bytes OR
   NOT actual_bytes EQUAL expected_bytes OR NOT actual_sha256 STREQUAL asset_sha256)
    message(FATAL_ERROR "Invalid idle animation pack; run tools/convert_animation.py")
endif()
idf_component_register(SRCS "app_main.c" "display.c" "pet_animation.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_timer
    EMBED_FILES "${asset_dir}/idle.rgb565")
configure_file("${CMAKE_CURRENT_LIST_DIR}/asset_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/asset_config.h" @ONLY)
