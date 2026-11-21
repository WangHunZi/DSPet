set(asset_dir "${CMAKE_CURRENT_LIST_DIR}/../assets/generated")
set(clip_names idle wave stretch dance eat sleep)
set(declarations "")
set(records "")
set(binaries "")
foreach(name IN LISTS clip_names)
    set(manifest "${asset_dir}/${name}.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")
    file(READ "${manifest}" asset_json)
    foreach(field width height fps frame_count bytes_per_frame decoded_bytes_per_frame keyframe_interval format sha256)
        string(JSON asset_${field} GET "${asset_json}" ${field})
    endforeach()
    set(binary "${asset_dir}/${name}.rgb565z")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${binary}")
    file(SIZE "${binary}" actual_bytes)
    file(SHA256 "${binary}" actual_sha256)
    if(NOT asset_format STREQUAL "INDEX8_DELTA_RGB565_ZLIB" OR
       NOT asset_width EQUAL 240 OR NOT asset_height EQUAL 240 OR
       NOT asset_frame_count GREATER 0 OR NOT asset_fps GREATER 0 OR asset_fps GREATER 60 OR
       NOT asset_bytes_per_frame EQUAL 115200 OR NOT asset_decoded_bytes_per_frame EQUAL 58112 OR
       NOT asset_keyframe_interval GREATER 0 OR asset_keyframe_interval GREATER asset_frame_count OR
       NOT actual_sha256 STREQUAL asset_sha256)
        message(FATAL_ERROR "Invalid ${name} pack; run tools/convert_animation.py")
    endif()
    string(JSON offset_count LENGTH "${asset_json}" offsets)
    math(EXPR expected_offset_count "${asset_frame_count} + 1")
    if(NOT offset_count EQUAL expected_offset_count)
        message(FATAL_ERROR "Invalid ${name} frame index length")
    endif()
    set(offsets "")
    set(previous -1)
    foreach(i RANGE 0 ${asset_frame_count})
        string(JSON offset GET "${asset_json}" offsets ${i})
        if((i EQUAL 0 AND NOT offset EQUAL 0) OR
           NOT offset GREATER previous OR offset GREATER actual_bytes)
            message(FATAL_ERROR "Invalid ${name} frame offset ${i}")
        endif()
        string(APPEND offsets "${offset},")
        set(previous ${offset})
    endforeach()
    if(NOT previous EQUAL actual_bytes)
        message(FATAL_ERROR "Invalid ${name} final frame offset")
    endif()
    string(APPEND declarations
        "extern const uint8_t ${name}_start[] asm(\"_binary_${name}_rgb565z_start\");\n"
        "extern const uint8_t ${name}_end[] asm(\"_binary_${name}_rgb565z_end\");\n"
        "static const uint32_t ${name}_offsets[] = {${offsets}};\n")
    string(APPEND records
        "    {\"${name}\", ${name}_start, ${name}_end, ${name}_offsets, ${asset_frame_count}, ${asset_fps}, ${asset_keyframe_interval}},\n")
    list(APPEND binaries "${binary}")
endforeach()
idf_component_register(
    SRCS "app_main.c" "app/DSPetActionState.c" "diagnostics/DSPetAudioCheck.c"
         "hardware/DSPetBootPin.c" "hardware/DSPetPanel.c" "hardware/DSPetAudioDevice.c"
         "services/input/DSPetButton.c" "services/input/DSPetButtonFilter.c"
         "services/animation/DSPetAnimation.c" "services/animation/DSPetIndexedFrame.c"
         "services/audio/DSPetPlayback.c" "ui/DSPetPetPage.c"
    PRIV_INCLUDE_DIRS "." "app" "hardware" "services/input" "services/animation" "services/audio" "ui" "diagnostics"
                      "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2s esp_timer esp_rom
    EMBED_FILES ${binaries})
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/asset_config.h"
    "#pragma once\n#include \"DSPetClip.h\"\n${declarations}"
    "static const DSPetClip pet_clips[] = {\n${records}};\n"
    "#define PET_CLIP_COUNT (sizeof(pet_clips) / sizeof(pet_clips[0]))\n")
