/* lv_conf toi thieu cho test host (tools/tests/test_font_render_host.py), gan voi sdkconfig.defaults. */
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE
#define LV_FONT_MONTSERRAT_14 1
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_CUSTOM_FONT_DEFAULT 1
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_LOG 0
#endif
