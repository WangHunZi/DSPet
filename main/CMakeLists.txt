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
         "services/audio/DSPetPlayback.c" "services/audio/DSPetPcmReplay.c" "ui/DSPetPetPage.c"
    PRIV_INCLUDE_DIRS "." "app" "hardware" "services/input" "services/animation" "services/audio" "ui" "diagnostics" "console"
                      "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES console esp_app_format esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2s esp_timer esp_rom
    EMBED_FILES ${binaries})
if(CONFIG_DSPET_AEC)
    if(NOT CONFIG_SPIRAM OR CONFIG_SPIRAM_USE_MEMMAP)
        message(FATAL_ERROR "DSPet AEC requires PSRAM registered with the heap capabilities allocator")
    endif()
    target_sources(${COMPONENT_LIB} PRIVATE "services/audio/DSPetAec.c" "services/audio/DSPetAecSync.c")
endif()
if(CONFIG_DSPET_PLAYBACK_DEMO OR CONFIG_DSPET_CONSOLE_TESTS)
    set(demo_source "${CMAKE_CURRENT_LIST_DIR}/app/DSPetPlaybackDemoAsset.S")
    set(demo_pcm "${PROJECT_DIR}/.cache/playback-demo.pcm")
    if(NOT EXISTS "${demo_pcm}")
        message(FATAL_ERROR "Playback demo PCM is missing. Run bash tools/make_playback_demo.sh")
    endif()
    set_source_files_properties("${demo_source}" PROPERTIES OBJECT_DEPENDS "${demo_pcm}")
    target_sources(${COMPONENT_LIB} PRIVATE "${demo_source}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DSPET_PLAYBACK_DEMO_PCM_PATH="${demo_pcm}")
endif()
if(CONFIG_DSPET_PLAYBACK_DEMO)
    target_sources(${COMPONENT_LIB} PRIVATE "app/DSPetPlaybackDemo.c")
endif()
if(CONFIG_DSPET_CONSOLE_TESTS)
    target_sources(${COMPONENT_LIB} PRIVATE "console/DSPetConsoleTests.c"
                   "../tests/hardware/DSPetDuplexTest.c" "../tests/human/DSPetLoopbackTest.c"
                   "../tests/hardware/DSPetAlignmentTest.c" "../tests/hardware/DSPetAudioCorrelation.c")
    target_include_directories(${COMPONENT_LIB} PRIVATE "../tests/hardware" "../tests/human")
    if(CONFIG_DSPET_AEC)
        target_sources(${COMPONENT_LIB} PRIVATE "../tests/hardware/DSPetAecTest.c")
    endif()
endif()
if(CONFIG_DSPET_AUDIO_DUPLEX_PROBE)
    target_sources(${COMPONENT_LIB} PRIVATE "diagnostics/DSPetDuplexProbe.c")
endif()
if(CONFIG_DSPET_SERIAL_SHELL)
    target_sources(${COMPONENT_LIB} PRIVATE "console/DSPetConsole.c")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/asset_config.h"
    "#pragma once\n#include \"DSPetClip.h\"\n${declarations}"
    "static const DSPetClip pet_clips[] = {\n${records}};\n"
    "#define PET_CLIP_COUNT (sizeof(pet_clips) / sizeof(pet_clips[0]))\n")
