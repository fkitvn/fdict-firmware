#include "./lv_i18n.h"


////////////////////////////////////////////////////////////////////////////////
// Define plural operands
// http://unicode.org/reports/tr35/tr35-numbers.html#Operands

// Integer version, simplified

#define UNUSED(x) (void)(x)

static inline uint32_t op_n(int32_t val) { return (uint32_t)(val < 0 ? -val : val); }
static inline uint32_t op_i(uint32_t val) { return val; }
// always zero, when decimal part not exists.
static inline uint32_t op_v(uint32_t val) { UNUSED(val); return 0;}
static inline uint32_t op_w(uint32_t val) { UNUSED(val); return 0; }
static inline uint32_t op_f(uint32_t val) { UNUSED(val); return 0; }
static inline uint32_t op_t(uint32_t val) { UNUSED(val); return 0; }

static lv_i18n_phrase_t en_singulars[] = {
    {"ABOUT", "About"},
    {"ALL", "All"},
    {"AUTO_PLAY", "Auto play"},
    {"BACK", "Back"},
    {"BUILD_DATE", "Build date"},
    {"CANCEL", "Cancel"},
    {"CARDS", "Cards"},
    {"CHANGE_PASSWORD", "Change password"},
    {"CHECKING", "Checking…"},
    {"CHECK_FOR_UPDATES", "Check for updates"},
    {"CHECK_WIFI_RETRY", "Check Wi-Fi and try again."},
    {"CHIP_INFO", "%s v%u.%u, %u cores, N%uR%u"},
    {"CLOSE", "Close"},
    {"COMING_SOON", "Coming soon"},
    {"CONNECT", "Connect"},
    {"CONNECTED", "Connected"},
    {"CONNECTING", "Connecting"},
    {"CONNECTING_TO_WIFI", "Connecting to WiFi"},
    {"CONNECTIVITY", "Connectivity"},
    {"CONTINUE", "Continue"},
    {"DATA", "Data"},
    {"DEVELOPER_MODE", "Developer mode"},
    {"DEVICE", "Device"},
    {"DEVICE_ID", "Device ID"},
    {"DEVICE_PAIRED", "Device paired"},
    {"DEVMODE_UNPAIR_WARN", "The device will be unpaired from your account; you will need to sign in again"},
    {"DIRECTION", "Direction"},
    {"DISPLAY", "Display"},
    {"DISPLAY_INFO", "%dx%d, rotated %d°"},
    {"ENDLESS", "Endless"},
    {"EN_VI", "EN-VI"},
    {"ERROR_FMT", "Error: %s"},
    {"FLASH_CARD", "Flash card"},
    {"FLASH_MEMORY", "Flash memory"},
    {"FORGET", "Forget"},
    {"FORGET_THIS_NETWORK", "Forget this network"},
    {"FRONT_SIDE", "Front side"},
    {"HOME", "Home"},
    {"INTERNAL_RAM", "Internal RAM"},
    {"INVALID_QUESTION_DATA", "Invalid question data"},
    {"IN_ORDER", "In order"},
    {"LATER", "Later"},
    {"LOADING_QUESTIONS", "Loading questions…"},
    {"MEANING", "Meaning"},
    {"MEMORY", "Memory"},
    {"MISSING", "Missing"},
    {"NEVER", "Never"},
    {"NEXT", "Next >"},
    {"NONE", "None"},
    {"NOT_CONNECTED", "Not connected"},
    {"NOT_PAIRED", "Not paired"},
    {"NOT_SET", "Not set"},
    {"NO_NETWORKS_FOUND", "No networks found"},
    {"NO_OFFLINE_DATA_YET", "No offline data yet"},
    {"NO_UPDATE_AVAILABLE", "No update available"},
    {"N_MIN", "%d min"},
    {"N_SEC", "%d sec"},
    {"OFF", "Off"},
    {"OFFLINE_CONNECT_HINT", "Connect Wi-Fi to download questions automatically."},
    {"OFFLINE_DATA_READY", "Offline data ready"},
    {"OFFLINE_NO_WIFI", "No Wi-Fi, studying offline"},
    {"ON", "On"},
    {"OPEN", "Open"},
    {"OPEN_WIFI", "Open Wi-Fi"},
    {"ORDER", "Order"},
    {"OTA_DOWNLOADING", "Downloading %d%%"},
    {"OTA_ERR_BAD_IMAGE", "invalid image"},
    {"OTA_ERR_BATT_LOW", "low battery"},
    {"OTA_ERR_HTTP", "server (%s)"},
    {"OTA_ERR_NET", "network"},
    {"OTA_ERR_NO_TIME", "clock not synced"},
    {"OTA_ERR_NO_WIFI", "WiFi not connected"},
    {"OTA_ERR_TOO_LARGE", "image too large"},
    {"OTA_ERR_VERIFY", "checksum mismatch"},
    {"OTA_NEED_WIFI", "Wi-Fi needed to check for updates"},
    {"OTA_NEW_VERSION", "New version %s (%u.%02u MB)"},
    {"OTA_RESTARTING", "Restarting to update…"},
    {"OTA_UPDATE_ASK", "New version %s/%s (%u.%02u MB). Update now?"},
    {"PAIR_DEVICE", "Pair device"},
    {"PAIR_HINT", "Pair the device with your account on your phone."},
    {"PAIR_TO_STUDY", "Pair the device to study"},
    {"PASSWORD", "Password"},
    {"PIN_CODE", "PIN code"},
    {"PREV", "< Prev"},
    {"QUESTIONS", "Questions"},
    {"QUIT", "Quit"},
    {"QUIT_THIS_QUIZ", "Quit this quiz?"},
    {"QUIZ_CORRECT_PCT", "correct · %d%%"},
    {"QUIZ_QUESTION_N", "Q %d"},
    {"QUIZ_QUESTION_N_OF", "Q %d/%d"},
    {"QUIZ_SCORE", "Score %d"},
    {"RANDOM", "Random"},
    {"RECONNECT", "Reconnect"},
    {"REPEAT_1", "1 time"},
    {"REPEAT_2", "2 times"},
    {"RESTART_NOW", "Restart now"},
    {"RESULT", "Result"},
    {"RETRY", "Retry"},
    {"SAVED", "Saved"},
    {"SCANNING", "Scanning…"},
    {"SCREEN_BRIGHTNESS", "Screen brightness"},
    {"SCREEN_ROTATION", "Screen rotation"},
    {"SCREEN_TIMEOUT", "Screen timeout"},
    {"SECURED", "Secured"},
    {"SERVER", "Server"},
    {"SERVER_ERROR_CODE", "Server error (%d)"},
    {"SERVER_UNREACHABLE", "Cannot reach the server"},
    {"SET", "Set"},
    {"SETTINGS", "Settings"},
    {"SHUFFLE", "Shuffle"},
    {"SIZE", "Size"},
    {"SOFTWARE", "Software"},
    {"START", "Start"},
    {"STATISTICS", "Statistics"},
    {"STUDY", "Study"},
    {"STUDY_NEEDS_WIFI", "Study needs Wi-Fi to download questions."},
    {"SWIPE_BACK_HINT", "Swipe from the left/right edge to go back"},
    {"SYNC", "Sync"},
    {"SYNC_PENDING_N", "%d waiting to sync"},
    {"TAP_TO_FLIP", "Tap to flip the card"},
    {"TAP_TO_SETUP_WIFI", "Tap to set up Wi-Fi"},
    {"TIMEOUT_WAKE_HINT", "Touch the screen or press a button to wake"},
    {"TURN_OFF_DEVELOPER_MODE", "Turn off developer mode?"},
    {"TURN_ON_DEVELOPER_MODE", "Turn on developer mode?"},
    {"UNPAIRED_LAB", "Lab server · unpaired"},
    {"UNPAIRED_OFFICIAL", "Official server · unpaired"},
    {"UNREADABLE", "Unreadable"},
    {"UPDATE", "Update"},
    {"UPGRADE", "Upgrade"},
    {"VI_EN", "VI-EN"},
    {"VOICE", "Voice"},
    {"VOLUME", "Volume"},
    {"WIFI_AUTH_FAILED", "Could not authenticate with router “%s” (code %d). Try again later."},
    {"WIFI_CONNECTING_TO", "Connecting to %s…"},
    {"WIFI_FORGET_ASK", "Forget network %s?"},
    {"WIFI_LOST_RETRYING", "Connection lost – retrying…"},
    {"WIFI_LOST_TAP_TO_OPEN", "WiFi lost - tap to open"},
    {"WIFI_NETWORK_NAME", "Network: %s"},
    {"WIFI_NOT_CONNECTED", "Wi-Fi not connected"},
    {"WIFI_NOT_FOUND", "Network “%s” not found"},
    {"WIFI_NO_SAVED", "No saved networks. Choose a network below."},
    {"WIFI_PASSWORD_SHORT", "WPA password needs at least 8 characters"},
    {"WIFI_SAVED_CONNECTED", "Saved · Connected to this network"},
    {"WIFI_SAVED_CONNECTING", "Saved · Connecting…"},
    {"WIFI_SAVED_NOT_CONNECTED", "Saved · Not connected"},
    {"WIFI_SECTION_AVAILABLE", "AVAILABLE NETWORKS"},
    {"WIFI_SECTION_SAVED", "SAVED"},
    {"WIFI_WRONG_PASSWORD", "Wrong password for “%s”. Select the network to re-enter it."},
    {"WILL_RESTART_WHEN_IDLE", "Will restart when idle"},
    {"WORD", "Word"},
    {NULL, NULL} // End mark
};



static uint8_t en_plural_fn(int32_t num)
{
    uint32_t n = op_n(num); UNUSED(n);
    uint32_t i = op_i(n); UNUSED(i);
    uint32_t v = op_v(n); UNUSED(v);

    if ((i == 1 && v == 0)) return LV_I18N_PLURAL_TYPE_ONE;
    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t en_lang = {
    .locale_name = "en",
    .singulars = en_singulars,

    .locale_plural_fn = en_plural_fn
};

static lv_i18n_phrase_t vi_singulars[] = {
    {"ABOUT", "Thông tin phiên bản"},
    {"ALL", "Tất cả"},
    {"AUTO_PLAY", "Tự động phát"},
    {"BACK", "Trở lại"},
    {"BUILD_DATE", "Ngày build"},
    {"CANCEL", "Hủy"},
    {"CARDS", "Số thẻ"},
    {"CHANGE_PASSWORD", "Đổi mật khẩu"},
    {"CHECKING", "Đang kiểm tra…"},
    {"CHECK_FOR_UPDATES", "Kiểm tra cập nhật"},
    {"CHECK_WIFI_RETRY", "Kiểm tra Wi-Fi rồi thử lại."},
    {"CHIP_INFO", "%s v%u.%u, %u nhân, N%uR%u"},
    {"CLOSE", "Đóng"},
    {"COMING_SOON", "Sắp có"},
    {"CONNECT", "Kết nối"},
    {"CONNECTED", "Đã kết nối"},
    {"CONNECTING", "Đang kết nối"},
    {"CONNECTING_TO_WIFI", "Đang kết nối WiFi"},
    {"CONNECTIVITY", "Kết nối"},
    {"CONTINUE", "Học tiếp"},
    {"DATA", "Dữ liệu"},
    {"DEVELOPER_MODE", "Chế độ nhà phát triển"},
    {"DEVICE", "Thiết bị"},
    {"DEVICE_ID", "Mã thiết bị"},
    {"DEVICE_PAIRED", "Đã ghép thiết bị"},
    {"DEVMODE_UNPAIR_WARN", "Thiết bị sẽ huỷ ghép nối với tài khoản, bạn sẽ cần đăng nhập lại"},
    {"DIRECTION", "Chiều"},
    {"DISPLAY", "Màn hình"},
    {"DISPLAY_INFO", "%dx%d, xoay %d°"},
    {"ENDLESS", "Vô tận"},
    {"EN_VI", "Anh-Việt"},
    {"ERROR_FMT", "Lỗi: %s"},
    {"FLASH_CARD", "Flash card"},
    {"FLASH_MEMORY", "Bộ nhớ flash"},
    {"FORGET", "Quên"},
    {"FORGET_THIS_NETWORK", "Quên mạng này"},
    {"FRONT_SIDE", "Mặt trước"},
    {"HOME", "Về Home"},
    {"INTERNAL_RAM", "RAM nội bộ"},
    {"INVALID_QUESTION_DATA", "Dữ liệu câu hỏi không hợp lệ"},
    {"IN_ORDER", "Theo danh sách"},
    {"LATER", "Để sau"},
    {"LOADING_QUESTIONS", "Đang tải câu hỏi…"},
    {"MEANING", "Nghĩa"},
    {"MEMORY", "Bộ nhớ"},
    {"MISSING", "Thiếu"},
    {"NEVER", "Không tắt"},
    {"NEXT", "Sau >"},
    {"NONE", "Không có"},
    {"NOT_CONNECTED", "Chưa kết nối"},
    {"NOT_PAIRED", "Chưa ghép"},
    {"NOT_SET", "Chưa đặt"},
    {"NO_NETWORKS_FOUND", "Không tìm thấy mạng nào"},
    {"NO_OFFLINE_DATA_YET", "Chưa có dữ liệu offline"},
    {"NO_UPDATE_AVAILABLE", "Không có bản nâng cấp"},
    {"N_MIN", "%d phút"},
    {"N_SEC", "%d giây"},
    {"OFF", "Tắt"},
    {"OFFLINE_CONNECT_HINT", "Kết nối Wi-Fi để tự tải câu hỏi."},
    {"OFFLINE_DATA_READY", "Có dữ liệu offline"},
    {"OFFLINE_NO_WIFI", "Không có Wifi, học offline"},
    {"ON", "Bật"},
    {"OPEN", "Mở"},
    {"OPEN_WIFI", "Mở Wi-Fi"},
    {"ORDER", "Thứ tự"},
    {"OTA_DOWNLOADING", "Đang tải %d%%"},
    {"OTA_ERR_BAD_IMAGE", "ảnh không hợp lệ"},
    {"OTA_ERR_BATT_LOW", "pin yếu"},
    {"OTA_ERR_HTTP", "máy chủ (%s)"},
    {"OTA_ERR_NET", "mạng"},
    {"OTA_ERR_NO_TIME", "chưa đồng bộ giờ"},
    {"OTA_ERR_NO_WIFI", "chưa kết nối WiFi"},
    {"OTA_ERR_TOO_LARGE", "bản quá lớn"},
    {"OTA_ERR_VERIFY", "sai mã kiểm tra"},
    {"OTA_NEED_WIFI", "Cần Wi-Fi để kiểm tra cập nhật"},
    {"OTA_NEW_VERSION", "Có bản mới %s (%u.%02u MB)"},
    {"OTA_RESTARTING", "Đang khởi động lại để cập nhật…"},
    {"OTA_UPDATE_ASK", "Có phiên bản mới %s/%s (%u.%02u MB). Nâng cấp ngay?"},
    {"PAIR_DEVICE", "Ghép thiết bị"},
    {"PAIR_HINT", "Ghép máy với tài khoản trên điện thoại."},
    {"PAIR_TO_STUDY", "Cần ghép thiết bị để học"},
    {"PASSWORD", "Mật khẩu"},
    {"PIN_CODE", "Mã PIN"},
    {"PREV", "< Trước"},
    {"QUESTIONS", "Số câu"},
    {"QUIT", "Thoát"},
    {"QUIT_THIS_QUIZ", "Thoát bài làm?"},
    {"QUIZ_CORRECT_PCT", "đúng · %d%%"},
    {"QUIZ_QUESTION_N", "Câu %d"},
    {"QUIZ_QUESTION_N_OF", "Câu %d/%d"},
    {"QUIZ_SCORE", "Điểm %d"},
    {"RANDOM", "Ngẫu nhiên"},
    {"RECONNECT", "Kết nối lại"},
    {"REPEAT_1", "1 lần"},
    {"REPEAT_2", "2 lần"},
    {"RESTART_NOW", "Khởi động lại ngay"},
    {"RESULT", "Kết quả"},
    {"RETRY", "Thử lại"},
    {"SAVED", "Đã lưu"},
    {"SCANNING", "Đang quét…"},
    {"SCREEN_BRIGHTNESS", "Độ sáng màn hình"},
    {"SCREEN_ROTATION", "Hướng xoay màn hình"},
    {"SCREEN_TIMEOUT", "Thời gian chờ màn hình"},
    {"SECURED", "Bảo mật"},
    {"SERVER", "Máy chủ"},
    {"SERVER_ERROR_CODE", "Máy chủ báo lỗi (%d)"},
    {"SERVER_UNREACHABLE", "Không kết nối được máy chủ"},
    {"SET", "Đã đặt"},
    {"SETTINGS", "Cài đặt"},
    {"SHUFFLE", "Trộn"},
    {"SIZE", "Kích thước"},
    {"SOFTWARE", "Phần mềm"},
    {"START", "Bắt đầu"},
    {"STATISTICS", "Thống kê"},
    {"STUDY", "Học tập"},
    {"STUDY_NEEDS_WIFI", "Học tập cần Wi-Fi để tải câu hỏi."},
    {"SWIPE_BACK_HINT", "Vuốt từ mép trái/phải để quay lại"},
    {"SYNC", "Đồng bộ"},
    {"SYNC_PENDING_N", "Chờ đồng bộ %d"},
    {"TAP_TO_FLIP", "Chạm để lật thẻ"},
    {"TAP_TO_SETUP_WIFI", "Chạm để cài Wi-Fi"},
    {"TIMEOUT_WAKE_HINT", "Chạm màn hình hoặc bấm nút để bật lại"},
    {"TURN_OFF_DEVELOPER_MODE", "Tắt chế độ nhà phát triển?"},
    {"TURN_ON_DEVELOPER_MODE", "Bật chế độ nhà phát triển?"},
    {"UNPAIRED_LAB", "Server Lab · đã huỷ ghép"},
    {"UNPAIRED_OFFICIAL", "Server chính thức · đã huỷ ghép"},
    {"UNREADABLE", "Không đọc được"},
    {"UPDATE", "Cập nhật"},
    {"UPGRADE", "Nâng cấp"},
    {"VI_EN", "Việt-Anh"},
    {"VOICE", "Giọng đọc"},
    {"VOLUME", "Âm lượng"},
    {"WIFI_AUTH_FAILED", "Không xác thực được với router “%s” (mã %d). Thử lại sau."},
    {"WIFI_CONNECTING_TO", "Đang kết nối %s…"},
    {"WIFI_FORGET_ASK", "Quên mạng %s?"},
    {"WIFI_LOST_RETRYING", "Mất kết nối – đang thử lại…"},
    {"WIFI_LOST_TAP_TO_OPEN", "Mất WiFi - chạm để mở"},
    {"WIFI_NETWORK_NAME", "Mạng: %s"},
    {"WIFI_NOT_CONNECTED", "Chưa kết nối Wi-Fi"},
    {"WIFI_NOT_FOUND", "Không tìm thấy mạng “%s”"},
    {"WIFI_NO_SAVED", "Chưa có mạng nào được lưu. Chọn mạng bên dưới."},
    {"WIFI_PASSWORD_SHORT", "Mật khẩu WPA cần ít nhất 8 ký tự"},
    {"WIFI_SAVED_CONNECTED", "Đã lưu · Đang kết nối với mạng này"},
    {"WIFI_SAVED_CONNECTING", "Đã lưu · Đang kết nối…"},
    {"WIFI_SAVED_NOT_CONNECTED", "Đã lưu · Chưa kết nối"},
    {"WIFI_SECTION_AVAILABLE", "MẠNG KHẢ DỤNG"},
    {"WIFI_SECTION_SAVED", "ĐÃ LƯU"},
    {"WIFI_WRONG_PASSWORD", "Sai mật khẩu cho “%s”. Chọn mạng để nhập lại."},
    {"WILL_RESTART_WHEN_IDLE", "Sẽ khởi động lại khi rảnh"},
    {"WORD", "Từ"},
    {NULL, NULL} // End mark
};



static uint8_t vi_plural_fn(int32_t num)
{



    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t vi_lang = {
    .locale_name = "vi",
    .singulars = vi_singulars,

    .locale_plural_fn = vi_plural_fn
};

const lv_i18n_language_pack_t lv_i18n_language_pack[] = {
    &en_lang,
    &vi_lang,
    NULL // End mark
};

////////////////////////////////////////////////////////////////////////////////


// Internal state
static const lv_i18n_language_pack_t * current_lang_pack;
static const lv_i18n_lang_t * current_lang;


/**
 * Reset internal state. For testing.
 */
void __lv_i18n_reset(void)
{
    current_lang_pack = NULL;
    current_lang = NULL;
}

/**
 * Set the languages for internationalization
 * @param langs pointer to the array of languages. (Last element has to be `NULL`)
 */
int lv_i18n_init(const lv_i18n_language_pack_t * langs)
{
    if(langs == NULL) return -1;
    if(langs[0] == NULL) return -1;

    current_lang_pack = langs;
    current_lang = langs[0];     /*Automatically select the first language*/
    return 0;
}

/**
 * Change the localization (language)
 * @param l_name name of the translation locale to use. E.g. "en-GB"
 */
int lv_i18n_set_locale(const char * l_name)
{
    if(current_lang_pack == NULL) return -1;

    uint16_t i;

    for(i = 0; current_lang_pack[i] != NULL; i++) {
        // Found -> finish
        if(strcmp(current_lang_pack[i]->locale_name, l_name) == 0) {
            current_lang = current_lang_pack[i];
            return 0;
        }
    }

    return -1;
}


static const char * __lv_i18n_get_text_core(lv_i18n_phrase_t * trans, const char * msg_id)
{
    uint16_t i;
    for(i = 0; trans[i].msg_id != NULL; i++) {
        if(strcmp(trans[i].msg_id, msg_id) == 0) {
            /*The msg_id has found. Check the translation*/
            if(trans[i].translation) return trans[i].translation;
        }
    }

    return NULL;
}


/**
 * Get the translation from a message ID
 * @param msg_id message ID
 * @return the translation of `msg_id` on the set local
 */
const char * lv_i18n_get_text(const char * msg_id)
{
    if(current_lang == NULL) return msg_id;

    const lv_i18n_lang_t * lang = current_lang;
    const void * txt;

    // Search in current locale
    if(lang->singulars != NULL) {
        txt = __lv_i18n_get_text_core(lang->singulars, msg_id);
        if (txt != NULL) return txt;
    }

    // Try to fallback
    if(lang == current_lang_pack[0]) return msg_id;
    lang = current_lang_pack[0];

    // Repeat search for default locale
    if(lang->singulars != NULL) {
        txt = __lv_i18n_get_text_core(lang->singulars, msg_id);
        if (txt != NULL) return txt;
    }

    return msg_id;
}

/**
 * Get the translation from a message ID and apply the language's plural rule to get correct form
 * @param msg_id message ID
 * @param num an integer to select the correct plural form
 * @return the translation of `msg_id` on the set local
 */
const char * lv_i18n_get_text_plural(const char * msg_id, int32_t num)
{
    if(current_lang == NULL) return msg_id;

    const lv_i18n_lang_t * lang = current_lang;
    const void * txt;
    lv_i18n_plural_type_t ptype;

    // Search in current locale
    if(lang->locale_plural_fn != NULL) {
        ptype = lang->locale_plural_fn(num);

        if(lang->plurals[ptype] != NULL) {
            txt = __lv_i18n_get_text_core(lang->plurals[ptype], msg_id);
            if (txt != NULL) return txt;
        }
    }

    // Try to fallback
    if(lang == current_lang_pack[0]) return msg_id;
    lang = current_lang_pack[0];

    // Repeat search for default locale
    if(lang->locale_plural_fn != NULL) {
        ptype = lang->locale_plural_fn(num);

        if(lang->plurals[ptype] != NULL) {
            txt = __lv_i18n_get_text_core(lang->plurals[ptype], msg_id);
            if (txt != NULL) return txt;
        }
    }

    return msg_id;
}

/**
 * Get the name of the currently used locale.
 * @return name of the currently used locale. E.g. "en-GB"
 */
const char * lv_i18n_get_current_locale(void)
{
    if(!current_lang) return NULL;
    return current_lang->locale_name;
}
