# Fdict firmware (ESP32-S3, board Freenove FNK0104B)
cmake_minimum_required(VERSION 3.16)


# Ten firmware = app version (esp_app_desc.version): "FDICT-1.0.6".
# Ma build hien thi luc chay = "10" + 4 ky tu hex dau cua SHA256 ELF (vd 10a3f9).
set(PROJECT_VER "FDICT-1.1.7")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Ten firmware duy nhat (tieu de, dong Firmware, QR, log khoi dong) = PROJECT_VER
idf_build_set_property(COMPILE_DEFINITIONS "FD_FW_NAME=\"${PROJECT_VER}\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "FD_BUILD_PREFIX=\"10\"" APPEND)

project(fdict-fw)

# 23/10: du lieu giong Pico (fd_tts) -> build/voice.bin -> phan vung 'voice'; `idf.py flash` nap ca phan vung nay.
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
