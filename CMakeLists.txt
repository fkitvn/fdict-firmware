# FDict One firmware (ESP32-S3, board Freenove FNK0104B)
cmake_minimum_required(VERSION 3.16)


# Ten firmware = app version (esp_app_desc.version, char[32]): "FDict One - x.y.z" (dinh dang duy nhat).
# Build ID = 10 ky tu hex dau (chu thuong) cua SHA256 file ELF (vd 3fa9c01b7e).
set(PROJECT_VER "FDict One - 1.3.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# FD_FW_NAME = PROJECT_VER (QR, log khoi dong, OTA); FD_FW_SEMVER = phan x.y.z.
# PROJECT_VER co dau cach -> khong truyen qua -D (de vo quoting tuy generator/shell);
# sinh header build/fd_fw_name.h roi -include cho moi component.
string(REGEX MATCH "([0-9]+\\.[0-9]+\\.[0-9]+)$" FD_FW_SEMVER "${PROJECT_VER}")
if(NOT FD_FW_SEMVER)
    message(FATAL_ERROR "PROJECT_VER phai ket thuc bang x.y.z: ${PROJECT_VER}")
endif()
set(FD_FW_NAME_H "${CMAKE_BINARY_DIR}/fd_fw_name.h")
file(CONFIGURE OUTPUT "${FD_FW_NAME_H}"
     CONTENT "#pragma once\n#define FD_FW_NAME \"@PROJECT_VER@\"\n#define FD_FW_SEMVER \"@FD_FW_SEMVER@\"\n" @ONLY)
# SHELL: giu "-include <file>" thanh mot cap (CMake khong khu trung lap tach roi)
idf_build_set_property(COMPILE_OPTIONS "SHELL:-include ${FD_FW_NAME_H}" APPEND)

project(fdict-firmware)

# Du lieu giong Pico (fd_tts) -> build/voice.bin -> phan vung voice; idf.py flash nap luon.
set(FD_LANG "${CMAKE_CURRENT_LIST_DIR}/components/fd_tts/pico/lang")
set(FD_VOICE_BIN "${CMAKE_BINARY_DIR}/voice.bin")
idf_build_get_property(fd_python PYTHON)
add_custom_command(OUTPUT "${FD_VOICE_BIN}"
    COMMAND ${fd_python} "${CMAKE_CURRENT_LIST_DIR}/tools/mk_voice.py" "${FD_LANG}/en-US_ta.bin" "${FD_LANG}/en-US_lh0_sg.bin" "${FD_VOICE_BIN}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tools/mk_voice.py" "${FD_LANG}/en-US_ta.bin" "${FD_LANG}/en-US_lh0_sg.bin"
    COMMENT "Dong goi voice.bin (Pico en-US)")
add_custom_target(fd_voice_bin ALL DEPENDS "${FD_VOICE_BIN}")
esptool_py_flash_to_partition(flash "voice" "${FD_VOICE_BIN}")
add_dependencies(flash fd_voice_bin)
