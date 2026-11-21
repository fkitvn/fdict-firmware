# Berry (MIT, https://github.com/berry-lang/berry, commit 9f6fbc9) - ma nguon goc trong src/, khong sua.
# port/: berry_conf.h (sandbox FDict One), be_port.c (khong he thong file), be_modtab.c (bang module).
# generate/: bang hang so sinh bang tools/coc cua Berry tu src + port (xem README.md khi cap nhat Berry).
file(GLOB BERRY_SRCS "src/*.c" "port/*.c")
idf_component_register(SRCS ${BERRY_SRCS}
                    INCLUDE_DIRS "src" "port")
# Ma nguon ngoai: tat canh bao rieng cho component nay (CI yeu cau 0 canh bao cho toan bo log build).
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
