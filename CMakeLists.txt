cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ADF_PATH} OR NOT EXISTS "$ENV{ADF_PATH}/components")
    message(FATAL_ERROR
        "ESP-ADF is not set up (ADF_PATH is empty or missing).\n"
        "This firmware needs ESP-IDF v5.5.4 + ESP-ADF v2.8 — not IDF v6.x.\n"
        "Open the IDF_v5.5.4 PowerShell shortcut, then from this folder run:\n"
        "  python scripts/build-flash.py --setup\n"
        "That clones ./esp-adf, patches IDF/ADF, and exports ADF_PATH.\n"
        "After that, either keep using build-flash.py, or:\n"
        "  $env:ADF_PATH = \"$PWD\\esp-adf\"\n"
        "  idf.py build")
endif()
set(EXTRA_COMPONENT_DIRS
    $ENV{ADF_PATH}/components
)

# ── Variant selection ────────────────────────────────────────────────────────
# Single source of truth for the HW variant: main/include/defines.h.
# We parse the active DISPLAY_* / TOUCH_* defines there and generate
# sdkconfig.variant so CMake/Kconfig stay in sync — no need to edit sdkconfig.
set(DEFINES_H "${CMAKE_CURRENT_SOURCE_DIR}/main/include/defines.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DEFINES_H}")
file(READ "${DEFINES_H}" _atlas_defines)
set(_atlas_defines "${_atlas_defines}\n")

string(REGEX MATCH "\n[ \t]*#define[ \t]+DISPLAY_(ILI9341|ST7789V|ST7796|ILI9488|CO5300|SSD1322|SSD1306)[^A-Za-z0-9_]"
       _ "${_atlas_defines}")
set(ATLAS_DISPLAY "${CMAKE_MATCH_1}")

string(REGEX MATCH "\n[ \t]*#define[ \t]+TOUCH_(CST816D|FT6336U|XPT2046|NONE)[^A-Za-z0-9_]"
       _ "${_atlas_defines}")
set(ATLAS_TOUCH "${CMAKE_MATCH_1}")

string(REGEX MATCH "\n[ \t]*#define[ \t]+FLASH_(16MB|4MB)[^A-Za-z0-9_]"
       _ "${_atlas_defines}")
set(ATLAS_FLASH "${CMAKE_MATCH_1}")

if(NOT ATLAS_DISPLAY)
    message(FATAL_ERROR "No active DISPLAY_* define in ${DEFINES_H}")
endif()
if(NOT ATLAS_TOUCH)
    message(FATAL_ERROR "No active TOUCH_* define in ${DEFINES_H}")
endif()
if(NOT ATLAS_FLASH)
    message(FATAL_ERROR "No active FLASH_* define in ${DEFINES_H}")
endif()

message(STATUS "AtlasRadio Mini variant: DISPLAY=${ATLAS_DISPLAY}, TOUCH=${ATLAS_TOUCH}, FLASH=${ATLAS_FLASH}")

# Variant key matching the CI release-asset name (build.yml matrix), e.g.
# "co5300-cst816d", "ili9341-xpt2046", or bare "ssd1322" for the touch-less panel:
# <display>-<touch>, or just <display> when there is no touch controller.
# Exposed at runtime as FW_VARIANT so the auto-update check can request exactly
# this variant's .bin — a foreign-variant image would brick the device. Consumed
# through an accessor (app_fw_variant()) so the prebuilt updater lib, built once
# ahead of time, reads it at runtime instead of baking a wrong compile-time value.
string(TOLOWER "${ATLAS_DISPLAY}" _atlas_disp_lower)
string(TOLOWER "${ATLAS_TOUCH}"   _atlas_touch_lower)
if(ATLAS_TOUCH STREQUAL "NONE")
    set(FW_VARIANT "${_atlas_disp_lower}")
else()
    set(FW_VARIANT "${_atlas_disp_lower}-${_atlas_touch_lower}")
endif()
if(ATLAS_FLASH STREQUAL "4MB")
    set(FW_VARIANT "s3mini-ssd1306")
endif()
message(STATUS "AtlasRadio Mini FW_VARIANT: ${FW_VARIANT}")
add_compile_definitions(FW_VARIANT="${FW_VARIANT}")

set(VARIANT_SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig.variant")
file(WRITE "${VARIANT_SDKCONFIG}"
    "# Auto-generated from main/include/defines.h — do not edit\n"
    "CONFIG_DISPLAY_${ATLAS_DISPLAY}=y\n"
    "CONFIG_TOUCH_${ATLAS_TOUCH}=y\n"
    "CONFIG_ESPTOOLPY_FLASHSIZE_${ATLAS_FLASH}=y\n"
    "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions${ATLAS_FLASH}.csv\"\n"
    "CONFIG_PARTITION_TABLE_FILENAME=\"partitions${ATLAS_FLASH}.csv\"\n"
)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;${VARIANT_SDKCONFIG}")
if(ATLAS_FLASH STREQUAL "4MB")
    set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS};sdkconfig.board.s3mini")
endif()
# ─────────────────────────────────────────────────────────────────────────────

include_directories(${CMAKE_SOURCE_DIR}/main/include)

# 4 MB Super Mini: do not compile DuerOS / speech / BT-A2DP / cloud RTC.
# audio_stream and esp_actions honour ATLAS_MINI_ADF (patched in those
# CMakeLists) so they no longer REQUIRES the excluded components.
if(ATLAS_FLASH STREQUAL "4MB")
    set(ATLAS_MINI_ADF ON CACHE BOOL "Strip unused ADF speech/cloud deps for 4 MB" FORCE)
    set(EXCLUDE_COMPONENTS
        esp-sr
        clouds
        dueros_service
        bluetooth_service
        esp_coze
        audio_recorder
        wifi_service
        ota_service
        battery_service
        coredump_upload_service
        audio_mixer
        input_key_service
    )
else()
    set(ATLAS_MINI_ADF OFF CACHE BOOL "Strip unused ADF speech/cloud deps for 4 MB" FORCE)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(IDF_COMPONENT_MANAGER_VERBOSE 1)

# IDF 5.5.5 ships GCC 14, which promotes -Wformat-truncation under -Werror=all.
# Tight-but-intentional snprintf buffers then fail the build. Keep the warning;
# do not treat it as an error (same idea as IDF's -Wno-error=unused-variable).
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format-truncation" APPEND)

# Derive firmware version from `git describe` (annotated tags).
# Embedded in the app descriptor and visible via esp_app_get_description().
set(PROJECT_VER_FROM_GIT 1)

project(atlascube)

if(ATLAS_FLASH STREQUAL "4MB")
    if(TARGET gen_project_binary)
        add_custom_command(TARGET gen_project_binary POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DBIN=${CMAKE_BINARY_DIR}/atlascube.bin
                -DMAX=3300000
                -P ${CMAKE_SOURCE_DIR}/cmake/check_app_size.cmake
            COMMENT "Checking 4 MB factory app size"
            VERBATIM
        )
    endif()
endif()

# ── LVGL heap pool → PSRAM ────────────────────────────────────────────────────
# CONFIG_LV_CONF_SKIP=y, so LVGL is configured entirely from Kconfig (there is no
# lv_conf.h) — and Kconfig exposes no knob for LV_ATTRIBUTE_LARGE_RAM_ARRAY.
# That attribute marks LVGL's builtin TLSF pool (work_mem_int, = LV_MEM_SIZE,
# 64 KB here); left empty it lands in the scarce internal DRAM. Force-include
# esp_attr.h and point the attribute at EXT_RAM_BSS_ATTR so the whole pool moves
# to PSRAM .bss (needs CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y). Only the
# object/style/anim pool moves — the display flush buffer is allocated separately
# in the drivers (DMA|INTERNAL), so render performance is unaffected.
idf_component_get_property(_lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${_lvgl_lib} PRIVATE -include esp_attr.h)
target_compile_definitions(${_lvgl_lib} PRIVATE LV_ATTRIBUTE_LARGE_RAM_ARRAY=EXT_RAM_BSS_ATTR)

# ── lv_display_flush_ready() → IRAM ───────────────────────────────────────────
# The ILI9341 and ST7789V drivers flush asynchronously and call flush_ready from
# the SPI done-ISR. CONFIG_SPI_MASTER_ISR_IN_IRAM=y, so that ISR keeps running
# while the flash cache is disabled (settings save, SPIFFS write, NVS commit) —
# and IDF 5.5 does not validate that a device's post_cb is IRAM-safe, so the
# fault would only show up as a rare crash under exactly that overlap. LVGL
# exposes LV_ATTRIBUTE_FLUSH_READY for this; Kconfig has no knob for it, hence
# the -D (both lv_display.h and lv_conf_internal.h guard it with #ifndef). The
# two functions it covers are one-line flag accessors, so the IRAM cost is a
# handful of bytes.
target_compile_definitions(${_lvgl_lib} PRIVATE LV_ATTRIBUTE_FLUSH_READY=IRAM_ATTR)

# Build both SPIFFS partition images so the merged release .bin (and a full
# `idf.py flash`) ships a working device:
#   www    — the compressed web UI. Run spiffs_image/tools/compress_web.py first;
#            build.py / CI do this automatically.
#   config — default user settings for a fresh device.
# USB app-flash never touches either partition; a full flash re-seeds them.
# For code-only dev iteration that must preserve on-device settings, flash just
# the factory app with `idf.py app-flash` instead of `idf.py flash`.
spiffs_create_partition_image(www    spiffs_image/web    FLASH_IN_PROJECT)
spiffs_create_partition_image(config spiffs_image/config FLASH_IN_PROJECT)

# GNU ld splits unquoted -L paths on spaces. ESP-ADF injects
#   -L${CMAKE_CURRENT_LIST_DIR}/...
# into the link line without quotes, and this repo lives under
# "CURSOR BT PLAYER+". Rewrite those flags to the 8.3 short path.
if(CMAKE_HOST_WIN32 AND CMAKE_SOURCE_DIR MATCHES " ")
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}" _atlas_native)
    execute_process(
        COMMAND powershell -NoProfile -Command
            "(New-Object -ComObject Scripting.FileSystemObject).GetFolder('${_atlas_native}').ShortPath"
        OUTPUT_VARIABLE ATLAS_SRC_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    file(TO_CMAKE_PATH "${ATLAS_SRC_SHORT}" ATLAS_SRC_SHORT)
    if(NOT ATLAS_SRC_SHORT OR ATLAS_SRC_SHORT MATCHES " ")
        message(FATAL_ERROR
            "This project path contains spaces (${CMAKE_SOURCE_DIR}) and Windows "
            "could not provide an 8.3 short name. ESP-ADF's linker flags then break.\n"
            "Move the folder to a path without spaces, e.g. C:/Users/jeanm/Documents/atlasradio-mini")
    endif()
    message(STATUS "Rewriting ADF -L flags to short path ${ATLAS_SRC_SHORT}")

    function(atlas_rewrite_link_dirs dir)
        get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_t IN LISTS _targets)
            if(NOT TARGET "${_t}")
                continue()
            endif()
            get_property(_alias TARGET "${_t}" PROPERTY ALIASED_TARGET)
            if(_alias)
                continue()
            endif()
            get_property(_type TARGET "${_t}" PROPERTY TYPE)
            if(NOT _type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY|INTERFACE_LIBRARY|EXECUTABLE)$")
                continue()
            endif()
            foreach(_prop INTERFACE_LINK_DIRECTORIES LINK_DIRECTORIES INTERFACE_LINK_OPTIONS LINK_OPTIONS IMPORTED_LOCATION INTERFACE_LINK_LIBRARIES LINK_LIBRARIES)
                get_property(_vals TARGET "${_t}" PROPERTY ${_prop})
                if(NOT _vals)
                    continue()
                endif()
                set(_out "")
                set(_changed FALSE)
                foreach(_item IN LISTS _vals)
                    # Leave IDF generator expressions ($<LINK_GROUP:RESCAN,...>)
                    # intact. Rewriting the whole property as one string empties
                    # --start-group and audio_engine cannot see ADF archives.
                    if(_item MATCHES "^\\$<")
                        list(APPEND _out "${_item}")
                        continue()
                    endif()
                    string(REPLACE "${CMAKE_SOURCE_DIR}" "${ATLAS_SRC_SHORT}" _rewritten "${_item}")
                    if(NOT _rewritten STREQUAL "${_item}")
                        set(_changed TRUE)
                    endif()
                    list(APPEND _out "${_rewritten}")
                endforeach()
                if(_changed)
                    set_property(TARGET "${_t}" PROPERTY ${_prop} "")
                    foreach(_item IN LISTS _out)
                        set_property(TARGET "${_t}" APPEND PROPERTY ${_prop} "${_item}")
                    endforeach()
                endif()
            endforeach()
        endforeach()
        get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(_s IN LISTS _subs)
            atlas_rewrite_link_dirs("${_s}")
        endforeach()
    endfunction()
    atlas_rewrite_link_dirs("${CMAKE_SOURCE_DIR}")
endif()

