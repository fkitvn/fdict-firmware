# FDict One firmware (ESP32-S3, board Freenove FNK0104B)
cmake_minimum_required(VERSION 3.16)


# Ten firmware = app version (esp_app_desc.version, char[32]): "FDict One - x.y.z" (dinh dang duy nhat).
# Build ID = 10 ky tu hex dau (chu thuong) cua SHA256 file ELF (vd 3fa9c01b7e).
set(PROJECT_VER "FDict One - 4.4.0")
# Hau to theo nhanh git (tu dong, KHONG sua tay): nhanh dung "dev" -> "FDict One - x.y.z-dev";
# moi nhanh/trang thai khac (prod, detached HEAD, CI tren prod, khong co git) -> giu nguyen.
# Nhanh: git rev-parse --abbrev-ref HEAD; neu detached ("HEAD") / loi -> env GITHUB_REF_NAME, roi CI_COMMIT_BRANCH.
# Ghi de: env FD_FW_SUFFIX (vd "" hoac "-dev"; chi chap nhan rong hoac -[a-z0-9]+).
# Dong set(PROJECT_VER ...) phia tren giu NGUYEN dang chu: scripts/ci-ota-build.sh doc no bang sed.
# Doi nhanh: .git/HEAD nam trong CMAKE_CONFIGURE_DEPENDS -> lan build ke tiep tu cau hinh lai.
set(FD_FW_SUFFIX "")
set(FD_GIT_BRANCH "")
set(FD_GIT_DIR "")
find_program(FD_GIT_EXE git)
if(FD_GIT_EXE)
    execute_process(COMMAND "${FD_GIT_EXE}" rev-parse --abbrev-ref HEAD
                    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                    OUTPUT_VARIABLE FD_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _fd_rc ERROR_QUIET)
    if(NOT _fd_rc EQUAL 0)
        set(FD_GIT_BRANCH "")
    endif()
    execute_process(COMMAND "${FD_GIT_EXE}" rev-parse --absolute-git-dir
                    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                    OUTPUT_VARIABLE FD_GIT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _fd_rc ERROR_QUIET)
    if(NOT _fd_rc EQUAL 0)
        set(FD_GIT_DIR "")
    endif()
endif()
if(FD_GIT_BRANCH STREQUAL "" OR FD_GIT_BRANCH STREQUAL "HEAD")
    if(NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
        set(FD_GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
    elseif(NOT "$ENV{CI_COMMIT_BRANCH}" STREQUAL "")
        set(FD_GIT_BRANCH "$ENV{CI_COMMIT_BRANCH}")
    endif()
endif()
if(FD_GIT_BRANCH STREQUAL "dev")
    set(FD_FW_SUFFIX "-dev")
endif()
if(DEFINED ENV{FD_FW_SUFFIX})
    set(FD_FW_SUFFIX "$ENV{FD_FW_SUFFIX}")
endif()
if(NOT FD_FW_SUFFIX MATCHES "^(-[a-z0-9]+)?$")
    message(FATAL_ERROR "FD_FW_SUFFIX phai rong hoac dang -[a-z0-9]+: '${FD_FW_SUFFIX}'")
endif()
if(FD_GIT_DIR AND EXISTS "${FD_GIT_DIR}/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FD_GIT_DIR}/HEAD")
endif()
string(APPEND PROJECT_VER "${FD_FW_SUFFIX}")
message(STATUS "FDict firmware: nhanh '${FD_GIT_BRANCH}' -> PROJECT_VER '${PROJECT_VER}'")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# FD_FW_NAME = PROJECT_VER (QR, log khoi dong, OTA); FD_FW_SEMVER = phan x.y.z.
# PROJECT_VER co dau cach -> khong truyen qua -D (de vo quoting tuy generator/shell);
# sinh header build/fd_fw_name.h roi -include cho moi component.
# Hau to tuy chon (-dev) sau x.y.z; FD_FW_SEMVER luon la x.y.z thuan.
if(PROJECT_VER MATCHES "([0-9]+\\.[0-9]+\\.[0-9]+)(-[a-z0-9]+)?$")
    set(FD_FW_SEMVER "${CMAKE_MATCH_1}")
else()
    message(FATAL_ERROR "PROJECT_VER phai ket thuc bang x.y.z (tuy chon hau to -dev): ${PROJECT_VER}")
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
