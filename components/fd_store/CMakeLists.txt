idf_component_register(SRCS "fd_store_fs.c" "fd_factory.c"
                    INCLUDE_DIRS "include"
                    REQUIRES joltwallet__littlefs nvs_flash)
