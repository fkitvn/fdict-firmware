# TTS tieng Anh tren may: SVOX Pico (Apache-2.0, pico/lib/NOTICE).
# 23/10: tai nguyen en-US KHONG con nhung trong app: tools/mk_voice.py dong goi -> build/voice.bin, nap vao phan vung
# 'voice' (partitions.csv); fd_tts.c mmap phan vung (khong sao chep). `idf.py flash` nap ca voice.bin.
file(GLOB PICO_SRCS "${CMAKE_CURRENT_LIST_DIR}/pico/lib/*.c")
idf_component_register(SRCS "fd_tts.c" ${PICO_SRCS}
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "pico/lib"
                    REQUIRES esp_timer heap esp_partition esp_rom)
# Ma Pico cu (chi pico/lib/*.c, khong ap dung cho fd_tts.c): tat 5 canh bao da biet, khong sua logic thu vien
set_source_files_properties(${PICO_SRCS} PROPERTIES COMPILE_OPTIONS
    "-Wno-implicit-fallthrough;-Wno-stringop-overflow;-Wno-varargs;-Wno-unused-but-set-variable;-Wno-unused-function")

