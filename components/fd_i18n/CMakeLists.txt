# Song ngu vi/en bang lv_i18n (github.com/lvgl/lv_i18n).
# lv_i18n.c/.h duoc SINH tu i18n/*.yml (xem README, muc "Ngon ngu giao dien") va duoc commit -> build khong can Node.
idf_component_register(SRCS "lv_i18n.c" "fd_i18n.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash)
