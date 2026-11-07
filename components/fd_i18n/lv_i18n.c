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
    {"NO", "No"},
    {"OFF", "Off"},
    {"ON", "On"},
    {"ABOUT", "About"},
    {"ALL", "All"},
    {"ALREADY_ON_HOME", "You are already on Home"},
    {"AUTO_PLAY", "Auto play"},
    {"BACK", "Back"},
    {"BRIGHTNESS", "Brightness"},
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
    {"DEVICE_PAIRED", "Signed in"},
    {"DEVMODE_UNPAIR_WARN", "The device will sign out of this account. You will need to sign in again."},
    {"DIRECTION", "Direction"},
    {"DISPLAY", "Display"},
    {"DISPLAY_INFO", "%dx%d, rotated %d°"},
    {"ENDLESS", "Endless"},
    {"EN_VI", "EN-VI"},
    {"ERROR_FMT", "Error: %s"},
    {"FLASH_CARD", "Flash card"},
    {"FLASH_MEMORY", "Flash memory"},
    {"FACTORY_RESET", "Factory reset"},
    {"FACTORY_RESET_BUSY", "Firmware update in progress"},
    {"FACTORY_RESET_CONFIRM", "Factory reset?"},
    {"FACTORY_RESET_DO", "Reset"},
    {"FACTORY_RESET_FAIL", "Could not reset"},
    {"FACTORY_RESET_MSG", "Signs out and erases Wi-Fi, PIN and data on this device, then restarts. Account words stay. Unsynced results are lost."},
    {"FACTORY_RESET_WORKING", "Resetting…"},
    {"FORGET", "Forget"},
    {"FORGET_THIS_NETWORK", "Forget this network"},
    {"FRONT_SIDE", "Front side"},
    {"GRAYSCALE_MODE", "Black & white"},
    {"GRAYSCALE_OFF", "Black & white off"},
    {"GRAYSCALE_ON", "Black & white on"},
    {"HELLO", "Hello!"},
    {"HELLO_NAME", "Hello, %s!"},
    {"HOME", "Home"},
    {"INTERNAL_RAM", "Internal RAM"},
    {"INVALID_QUESTION_DATA", "Invalid question data"},
    {"IN_ORDER", "In order"},
    {"LANGUAGE", "Language"},
    {"LATER", "Later"},
    {"LOADING", "Loading..."},
    {"LOADING_QUESTIONS", "Loading questions…"},
    {"LOGOUT", "Log out"},
    {"MEANING", "Meaning"},
    {"MEMORY", "Memory"},
    {"MIC_DELETE", "Delete"},
    {"MIC_FAILED", "Recording failed"},
    {"MIC_HINT", "Records up to 5 seconds"},
    {"MIC_MUTED", "Volume is off"},
    {"MIC_NO_FILE", "No recording yet"},
    {"MIC_NO_SPACE", "Not enough storage"},
    {"MIC_PLAY", "Play"},
    {"MIC_PLAYING", "Playing..."},
    {"MIC_RECORD", "Record"},
    {"MIC_RECORDING", "Recording..."},
    {"MIC_SAVED", "Recording: %d.%d s"},
    {"MIC_STOP", "Stop"},
    {"MIC_TEST", "Speaker & mic test"},
    {"MISSING", "Missing"},
    {"NEVER", "Never"},
    {"NEXT", "Next >"},
    {"NONE", "None"},
    {"NOT_CONNECTED", "Not connected"},
    {"NOT_PAIRED", "Not signed in"},
    {"NOT_SET", "Not set"},
    {"NO_NETWORKS_FOUND", "No networks found"},
    {"NO_OFFLINE_DATA_YET", "No offline data yet"},
    {"NO_UPDATE_AVAILABLE", "No update available"},
    {"NO_WIFI", "no Wi-Fi"},
    {"N_MIN", "%d min"},
    {"N_SEC", "%d sec"},
    {"OFFLINE_CONNECT_HINT", "Connect Wi-Fi to download questions automatically."},
    {"OFFLINE_DATA_READY", "Offline data ready"},
    {"OFFLINE_NO_WIFI", "No Wi-Fi, studying offline"},
    {"OPEN", "Open"},
    {"OPEN_WIFI", "Open Wi-Fi"},
    {"ORDER", "Order"},
    {"OTA_CONNECTING", "Connecting..."},
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
    {"PAIRED_WITH_ACCOUNT", "Signed in to account"},
    {"PAIR_CONNECT_WIFI", "Connect to Wi-Fi to sign in"},
    {"PAIR_DEVICE", "Sign in to continue"},
    {"PAIR_GETTING_CODE", "Getting code…"},
    {"PAIR_GET_CODE", "Get code"},
    {"PAIR_GET_NEW_CODE", "Get new code"},
    {"PAIR_HINT", "Sign in to continue"},
    {"PAIR_SCAN_WITH_PHONE", "Scan with your phone · %d:%02d"},
    {"PAIR_STEP_ID", "Sign in"},
    {"PAIR_STEP_ZALO", "Activate warranty"},
    {"POLICY_QR", "Return policy"},
    {"PAIR_TO_STUDY", "You are not signed in"},
    {"WORD_TYPE", "Word type"},
    {"FILL_BLANK", "Fill in the blank"},
    {"DICTIONARY", "Dictionary"},
    {"POS_PROMPT", "What type of word is this?"},
    {"LOOKUP_HINT", "Type an English word to look up"},
    {"LOOKUP_HISTORY", "Recent lookups"},
    {"LOOKUP_ADDING", "Adding to your study list…"},
    {"LOOKUP_ADDED", "Added to your study list"},
    {"LOOKUP_QUEUED", "Queued — will add when online"},
    {"LOOKUP_NEED_WIFI", "Wi-Fi needed to look up words"},
    {"LOOKUP_NOT_FOUND", "Word not found"},
    {"LOOKUP_ERROR", "Lookup failed"},
    {"PASSWORD", "Password"},
    {"PIN_CODE", "PIN code"},
    {"PIN_ENTER_CURRENT", "Enter current PIN"},
    {"PIN_ENTER_NEW", "Enter new PIN"},
    {"PIN_ENTER_TO_UNLOCK", "Enter PIN to unlock"},
    {"PIN_LOCKED_RETRY_IN", "Locked, try again in %d s"},
    {"PIN_MISMATCH", "PINs do not match. Enter new PIN"},
    {"PIN_REENTER_NEW", "Re-enter new PIN"},
    {"PIN_REMOVE", "Remove PIN"},
    {"PIN_REMOVED", "PIN removed"},
    {"PIN_REMOVE_ERROR", "Failed to remove PIN"},
    {"PIN_SAVED", "PIN saved"},
    {"PIN_SAVE_ERROR", "Failed to save PIN"},
    {"PIN_WRONG_LEFT", "Wrong PIN, %d tries left"},
    {"PRESS_ANY_BUTTON_TO_WAKE", "Press any button to wake up"},
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
    {"SLEEP", "Sleep"},
    {"SLEEP_CONFIRM", "Sleep?"},
    {"SOFTWARE", "Software"},
    {"SPLASH_TAGLINE", "FDict One - Vocabulary learning device"},
    {"START", "Start"},
    {"STATISTICS", "Statistics"},
    {"STUDY", "Quiz"},
    {"STUDY_NEEDS_WIFI", "Quiz needs Wi-Fi to download questions."},
    {"SWIPE_BACK_HINT", "Swipe from the left/right edge to go back"},
    {"SYNC", "Sync"},
    {"SYNC_PENDING_N", "%d waiting to sync"},
    {"TAP_TO_FLIP", "1 flips, 2 closes, 3 next"},
    {"TAP_TO_SETUP_WIFI", "Tap to set up Wi-Fi"},
    {"TIMEOUT_WAKE_HINT", "Touch the screen or press a button to wake"},
    {"TURN_OFF_DEVELOPER_MODE", "Turn off developer mode?"},
    {"TURN_ON_DEVELOPER_MODE", "Turn on developer mode?"},
    {"UNPAIR", "Sign out"},
    {"UNPAIRED_LAB", "Lab server · signed out"},
    {"UNPAIRED_OFFICIAL", "Official server · signed out"},
    {"UNPAIR_CONFIRM", "Sign out?"},
    {"UNPAIR_DEVICE", "Sign out"},
    {"UNPAIR_MSG", "The device will sign out of this account."},
    {"UNREADABLE", "Unreadable"},
    {"UPDATE", "Update"},
    {"UPGRADE", "Upgrade"},
    {"VI_EN", "VI-EN"},
    {"VOICE", "Voice"},
    {"VOICE_DATA_MISSING", "Voice data missing"},
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
    {"WARRANTY", "Warranty"},
    {"WARRANTY_NEEDED", "Not activated"},
    {"WARRANTY_UNTIL", "Until %s"},
    {"WARRANTY_ZALO_HINT", "Open FDict in the Zalo Mini App to activate warranty"},
    {"WILL_RESTART_WHEN_IDLE", "Will restart when idle"},
    {"WORD", "Word"},
    {"ZALO_CODE_ERROR", "Could not get a code"},
    {"ZALO_CODE_EXPIRED", "Code expired"},
    {"ZALO_CODE_EXPIRES", "Expires in %d:%02d"},
    {"ZALO_CODE_HINT", "Enter this code in the FDict Zalo Mini App to activate warranty"},
    {"ZALO_GETTING_CODE", "Getting code..."},
    {"ZALO_LINKED", "Zalo linked"},
    {"ZALO_NEW_CODE", "New code"},
    {"ZALO_WAITING", "Zalo not linked"},
    {"SENTENCE", "Sentence order"},
    {"SEN_LOADING", "Loading sentences…"},
    {"SEN_PROGRESS", "Sentence %d/%d"},
    {"SEN_PREV", "Prev"},
    {"SEN_NEXT", "Next"},
    {"SEN_UNDO", "Undo"},
    {"SEN_PICK", "Pick"},
    {"SEN_CORRECT", "Correct!"},
    {"SEN_WRONG", "Not quite"},
    {"SEN_ANSWER_IS", "Answer:"},
    {"SEN_CONTINUE", "Next"},
    {"SEN_RESULT", "%d/%d correct"},
    {"SEN_AGAIN", "Play again"},
    {"SEN_TAP_HINT", "Tap the words in order"},
    {"SEN_OFFLINE", "Offline mode"},
    {"SEN_SYNC_LATER", "Results will sync when online"},
    {"HOME_SYNCED_AT", "Synced at %s"},
    {"SEN_PLAY_AUDIO", "Play audio after each sentence"},
    {"SEN_COUNT", "Sentences per session"},
    {"SEN_DIR", "Direction"},
    {"SEN_DIR_EN_VI", "English > Vietnamese"},
    {"SEN_DIR_VI_EN", "Vietnamese > English"},
    {"DEV_TOOLS", "Test tools"},
    {"BTN_TEST", "Button test"},
    {"BTN_TEST_HINT", "Press each button below the screen in turn"},
    {"BTN_TEST_KEY", "Button %d"},
    {"BTN_TEST_PROGRESS", "Received %d/4 buttons"},
    {"BTN_TEST_PASS", "All buttons work well"},
    {"BTN_TEST_EXIT_HINT", "Hold Button 1 to exit"},
    {"BTN_TEST_RESET", "Reset"},
    {"SPK_TEST", "Speaker"},
    {"SPK_TONE", "1 kHz tone"},
    {"SPK_SWEEP", "Sweep"},
    {"SPK_STOP", "Stop"},
    {"SPK_PLAYING", "Playing test sound..."},
    {"MIC_SECTION", "Microphone"},
    {"DEV_SECTION", "For developers"},
    {"SYNC_NOW", "Sync now"},
    {"SYNC_WORDS", "Words"},
    {"SYNC_SENTENCES", "Sentences"},
    {"SYNC_COLLECTIONS", "Collections"},
    {"SYNC_LAST", "Last sync"},
    {"SYNC_PENDING", "Unsent results"},
    {"SYNC_STATE_OK", "Up to date"},
    {"SYNC_STATE_RUN", "Syncing…"},
    {"SYNC_STATE_PULL", "Downloading %d%%"},
    {"SYNC_STATE_IDLE", "Waiting for Wi-Fi"},
    {"SYNC_NEVER", "Never"},
    {"SYNC_AUTO_HINT", "Syncs automatically when Wi-Fi is on"},
    {"STATS_NEED_WIFI", "Wi-Fi is needed to view statistics"},
    {"STATS_LOADING", "Loading statistics…"},
    {"STATS_WORDS", "Total words"},
    {"STATS_MASTERED", "Mastered"},
    {"STATS_LEARNING", "Learning"},
    {"STATS_NEW", "New"},
    {"STATS_TODAY", "Today"},
    {"STATS_WEEK", "Last 7 days"},
    {"STATS_STREAK", "Study streak"},
    {"STATS_ANS_FMT", "%d/%d correct"},
    {"STATS_DAYS_FMT", "%d days"},
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
    {"NO", "Không"},
    {"OFF", "Tắt"},
    {"ON", "Bật"},
    {"ABOUT", "Thông tin phiên bản"},
    {"ALL", "Tất cả"},
    {"ALREADY_ON_HOME", "Bạn đã ở màn hình Home"},
    {"AUTO_PLAY", "Tự động phát"},
    {"BACK", "Trở lại"},
    {"BRIGHTNESS", "Độ sáng"},
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
    {"DEVICE_PAIRED", "Đã đăng nhập"},
    {"DEVMODE_UNPAIR_WARN", "Thiết bị sẽ đăng xuất khỏi tài khoản, bạn sẽ cần đăng nhập lại"},
    {"DIRECTION", "Chiều"},
    {"DISPLAY", "Màn hình"},
    {"DISPLAY_INFO", "%dx%d, xoay %d°"},
    {"ENDLESS", "Vô tận"},
    {"EN_VI", "Anh-Việt"},
    {"ERROR_FMT", "Lỗi: %s"},
    {"FLASH_CARD", "Lật từ"},
    {"FLASH_MEMORY", "Bộ nhớ flash"},
    {"FACTORY_RESET", "Khôi phục cài đặt gốc"},
    {"FACTORY_RESET_BUSY", "Đang cập nhật firmware"},
    {"FACTORY_RESET_CONFIRM", "Khôi phục cài đặt gốc?"},
    {"FACTORY_RESET_DO", "Khôi phục"},
    {"FACTORY_RESET_FAIL", "Không khôi phục được"},
    {"FACTORY_RESET_MSG", "Đăng xuất, xoá Wi-Fi, PIN và dữ liệu trên máy rồi khởi động lại. Từ trên tài khoản vẫn còn. Kết quả chưa đồng bộ sẽ mất."},
    {"FACTORY_RESET_WORKING", "Đang khôi phục…"},
    {"FORGET", "Quên"},
    {"FORGET_THIS_NETWORK", "Quên mạng này"},
    {"FRONT_SIDE", "Mặt trước"},
    {"GRAYSCALE_MODE", "Chế độ đen trắng"},
    {"GRAYSCALE_OFF", "Đã tắt chế độ đen trắng"},
    {"GRAYSCALE_ON", "Đã bật chế độ đen trắng"},
    {"HELLO", "Xin chào!"},
    {"HELLO_NAME", "Xin chào, %s!"},
    {"HOME", "Về Home"},
    {"INTERNAL_RAM", "RAM nội bộ"},
    {"INVALID_QUESTION_DATA", "Dữ liệu câu hỏi không hợp lệ"},
    {"IN_ORDER", "Theo danh sách"},
    {"LANGUAGE", "Ngôn ngữ"},
    {"LATER", "Để sau"},
    {"LOADING", "Đang tải..."},
    {"LOADING_QUESTIONS", "Đang tải câu hỏi…"},
    {"LOGOUT", "Đăng xuất"},
    {"MEANING", "Nghĩa"},
    {"MEMORY", "Bộ nhớ"},
    {"MIC_DELETE", "Xóa"},
    {"MIC_FAILED", "Ghi âm thất bại"},
    {"MIC_HINT", "Ghi âm tối đa 5 giây"},
    {"MIC_MUTED", "Âm lượng đang tắt"},
    {"MIC_NO_FILE", "Chưa có bản ghi"},
    {"MIC_NO_SPACE", "Không đủ bộ nhớ"},
    {"MIC_PLAY", "Nghe lại"},
    {"MIC_PLAYING", "Đang phát..."},
    {"MIC_RECORD", "Ghi âm"},
    {"MIC_RECORDING", "Đang ghi âm..."},
    {"MIC_SAVED", "Bản ghi: %d.%d giây"},
    {"MIC_STOP", "Dừng"},
    {"MIC_TEST", "Thử loa & micro"},
    {"MISSING", "Thiếu"},
    {"NEVER", "Không tắt"},
    {"NEXT", "Sau >"},
    {"NONE", "Không có"},
    {"NOT_CONNECTED", "Chưa kết nối"},
    {"NOT_PAIRED", "Chưa đăng nhập"},
    {"NOT_SET", "Chưa đặt"},
    {"NO_NETWORKS_FOUND", "Không tìm thấy mạng nào"},
    {"NO_OFFLINE_DATA_YET", "Chưa có dữ liệu offline"},
    {"NO_UPDATE_AVAILABLE", "Không có bản nâng cấp"},
    {"NO_WIFI", "chưa có Wi-Fi"},
    {"N_MIN", "%d phút"},
    {"N_SEC", "%d giây"},
    {"OFFLINE_CONNECT_HINT", "Kết nối Wi-Fi để tự tải câu hỏi."},
    {"OFFLINE_DATA_READY", "Có dữ liệu offline"},
    {"OFFLINE_NO_WIFI", "Không có Wifi, học offline"},
    {"OPEN", "Mở"},
    {"OPEN_WIFI", "Mở Wi-Fi"},
    {"ORDER", "Thứ tự"},
    {"OTA_CONNECTING", "Đang kết nối..."},
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
    {"PAIRED_WITH_ACCOUNT", "Đã đăng nhập tài khoản"},
    {"PAIR_CONNECT_WIFI", "Kết nối Wi-Fi để đăng nhập"},
    {"PAIR_DEVICE", "Đăng nhập để tiếp tục"},
    {"PAIR_GETTING_CODE", "Đang lấy mã…"},
    {"PAIR_GET_CODE", "Lấy mã"},
    {"PAIR_GET_NEW_CODE", "Lấy mã mới"},
    {"PAIR_HINT", "Đăng nhập để tiếp tục sử dụng"},
    {"PAIR_SCAN_WITH_PHONE", "Quét bằng điện thoại · %d:%02d"},
    {"PAIR_STEP_ID", "Đăng nhập"},
    {"PAIR_STEP_ZALO", "Kích hoạt bảo hành"},
    {"POLICY_QR", "Chính sách đổi trả"},
    {"PAIR_TO_STUDY", "Bạn chưa đăng nhập"},
    {"WORD_TYPE", "Phân loại từ"},
    {"FILL_BLANK", "Điền từ"},
    {"DICTIONARY", "Từ điển"},
    {"POS_PROMPT", "Từ này thuộc loại nào?"},
    {"LOOKUP_HINT", "Nhập từ tiếng Anh để tra"},
    {"LOOKUP_HISTORY", "Đã tra gần đây"},
    {"LOOKUP_ADDING", "Đang thêm vào từ cần học…"},
    {"LOOKUP_ADDED", "Đã thêm vào từ cần học"},
    {"LOOKUP_QUEUED", "Đã xếp hàng, sẽ thêm khi có mạng"},
    {"LOOKUP_NEED_WIFI", "Cần Wi-Fi để tra từ"},
    {"LOOKUP_NOT_FOUND", "Không tìm thấy từ này"},
    {"LOOKUP_ERROR", "Không tra được từ"},
    {"PASSWORD", "Mật khẩu"},
    {"PIN_CODE", "Mã PIN"},
    {"PIN_ENTER_CURRENT", "Nhập mã PIN hiện tại"},
    {"PIN_ENTER_NEW", "Nhập mã PIN mới"},
    {"PIN_ENTER_TO_UNLOCK", "Nhập để mở khóa"},
    {"PIN_LOCKED_RETRY_IN", "Đã khóa, thử lại sau %d giây"},
    {"PIN_MISMATCH", "Không khớp. Nhập mã PIN mới"},
    {"PIN_REENTER_NEW", "Nhập lại mã PIN mới"},
    {"PIN_REMOVE", "Xoá mã PIN"},
    {"PIN_REMOVED", "Đã xoá mã PIN"},
    {"PIN_REMOVE_ERROR", "Lỗi xoá mã PIN"},
    {"PIN_SAVED", "Đã lưu mã PIN"},
    {"PIN_SAVE_ERROR", "Lỗi lưu mã PIN"},
    {"PIN_WRONG_LEFT", "Sai mã PIN, còn %d lần"},
    {"PRESS_ANY_BUTTON_TO_WAKE", "Bấm nút bất kỳ để bật lại"},
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
    {"SLEEP", "Ngủ"},
    {"SLEEP_CONFIRM", "Ngủ?"},
    {"SOFTWARE", "Phần mềm"},
    {"SPLASH_TAGLINE", "FDict One - Máy học từ vựng"},
    {"START", "Bắt đầu"},
    {"STATISTICS", "Thống kê"},
    {"STUDY", "Quiz"},
    {"STUDY_NEEDS_WIFI", "Quiz cần Wi-Fi để tải câu hỏi."},
    {"SWIPE_BACK_HINT", "Vuốt từ mép trái/phải để quay lại"},
    {"SYNC", "Đồng bộ"},
    {"SYNC_PENDING_N", "Chờ đồng bộ %d"},
    {"TAP_TO_FLIP", "1 lật, 2 úp, 3 sang thẻ"},
    {"TAP_TO_SETUP_WIFI", "Chạm để cài Wi-Fi"},
    {"TIMEOUT_WAKE_HINT", "Chạm màn hình hoặc bấm nút để bật lại"},
    {"TURN_OFF_DEVELOPER_MODE", "Tắt chế độ nhà phát triển?"},
    {"TURN_ON_DEVELOPER_MODE", "Bật chế độ nhà phát triển?"},
    {"UNPAIR", "Đăng xuất"},
    {"UNPAIRED_LAB", "Server Lab · đã đăng xuất"},
    {"UNPAIRED_OFFICIAL", "Server chính thức · đã đăng xuất"},
    {"UNPAIR_CONFIRM", "Đăng xuất?"},
    {"UNPAIR_DEVICE", "Đăng xuất"},
    {"UNPAIR_MSG", "Máy sẽ đăng xuất khỏi tài khoản này."},
    {"UNREADABLE", "Không đọc được"},
    {"UPDATE", "Cập nhật"},
    {"UPGRADE", "Nâng cấp"},
    {"VI_EN", "Việt-Anh"},
    {"VOICE", "Giọng đọc"},
    {"VOICE_DATA_MISSING", "Thiếu dữ liệu giọng đọc"},
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
    {"WARRANTY", "Bảo hành"},
    {"WARRANTY_NEEDED", "Chưa kích hoạt"},
    {"WARRANTY_UNTIL", "Đến %s"},
    {"WARRANTY_ZALO_HINT", "Mở Fdict trên mini app zalo để kích hoạt bảo hành"},
    {"WILL_RESTART_WHEN_IDLE", "Sẽ khởi động lại khi rảnh"},
    {"WORD", "Từ"},
    {"ZALO_CODE_ERROR", "Không lấy được mã"},
    {"ZALO_CODE_EXPIRED", "Mã đã hết hạn"},
    {"ZALO_CODE_EXPIRES", "Hết hạn sau %d:%02d"},
    {"ZALO_CODE_HINT", "Nhập mã này trong Mini App FDict trên Zalo để kích hoạt bảo hành"},
    {"ZALO_GETTING_CODE", "Đang lấy mã..."},
    {"ZALO_LINKED", "Đã liên kết Zalo"},
    {"ZALO_NEW_CODE", "Lấy mã mới"},
    {"ZALO_WAITING", "Chưa liên kết Zalo"},
    {"SENTENCE", "Sắp xếp câu"},
    {"SEN_LOADING", "Đang tải câu…"},
    {"SEN_PROGRESS", "Câu %d/%d"},
    {"SEN_PREV", "Trước"},
    {"SEN_NEXT", "Sau"},
    {"SEN_UNDO", "Xóa"},
    {"SEN_PICK", "Chọn"},
    {"SEN_CORRECT", "Chính xác!"},
    {"SEN_WRONG", "Chưa đúng"},
    {"SEN_ANSWER_IS", "Đáp án:"},
    {"SEN_CONTINUE", "Tiếp"},
    {"SEN_RESULT", "Đúng %d/%d câu"},
    {"SEN_AGAIN", "Học tiếp"},
    {"SEN_TAP_HINT", "Chạm các từ theo đúng thứ tự"},
    {"SEN_OFFLINE", "Đang học ngoại tuyến"},
    {"SEN_SYNC_LATER", "Kết quả sẽ đồng bộ khi có mạng"},
    {"HOME_SYNCED_AT", "Đã đồng bộ vào lúc %s"},
    {"SEN_PLAY_AUDIO", "Phát âm thanh sau mỗi câu"},
    {"SEN_COUNT", "Số câu mỗi lượt"},
    {"SEN_DIR", "Hướng dịch"},
    {"SEN_DIR_EN_VI", "Anh > Việt"},
    {"SEN_DIR_VI_EN", "Việt > Anh"},
    {"DEV_TOOLS", "Công cụ kiểm tra"},
    {"BTN_TEST", "Thử nút bấm"},
    {"BTN_TEST_HINT", "Nhấn lần lượt từng nút bên dưới màn hình"},
    {"BTN_TEST_KEY", "Nút %d"},
    {"BTN_TEST_PROGRESS", "Đã nhận %d/4 nút"},
    {"BTN_TEST_PASS", "Tất cả nút hoạt động tốt"},
    {"BTN_TEST_EXIT_HINT", "Giữ Nút 1 để thoát"},
    {"BTN_TEST_RESET", "Làm lại"},
    {"SPK_TEST", "Loa"},
    {"SPK_TONE", "Âm 1 kHz"},
    {"SPK_SWEEP", "Quét tần số"},
    {"SPK_STOP", "Dừng"},
    {"SPK_PLAYING", "Đang phát âm thử..."},
    {"MIC_SECTION", "Micro"},
    {"DEV_SECTION", "Dành cho nhà phát triển"},
    {"SYNC_NOW", "Đồng bộ ngay"},
    {"SYNC_WORDS", "Từ vựng"},
    {"SYNC_SENTENCES", "Câu ví dụ"},
    {"SYNC_COLLECTIONS", "Bộ từ"},
    {"SYNC_LAST", "Lần đồng bộ cuối"},
    {"SYNC_PENDING", "Kết quả chờ gửi"},
    {"SYNC_STATE_OK", "Đã đồng bộ"},
    {"SYNC_STATE_RUN", "Đang đồng bộ…"},
    {"SYNC_STATE_PULL", "Đang tải %d%%"},
    {"SYNC_STATE_IDLE", "Chờ Wi-Fi"},
    {"SYNC_NEVER", "Chưa đồng bộ"},
    {"SYNC_AUTO_HINT", "Tự động đồng bộ khi có Wi-Fi"},
    {"STATS_NEED_WIFI", "Cần kết nối Wi-Fi để xem thống kê"},
    {"STATS_LOADING", "Đang tải thống kê…"},
    {"STATS_WORDS", "Tổng số từ"},
    {"STATS_MASTERED", "Đã thuộc"},
    {"STATS_LEARNING", "Đang học"},
    {"STATS_NEW", "Từ mới"},
    {"STATS_TODAY", "Hôm nay"},
    {"STATS_WEEK", "7 ngày qua"},
    {"STATS_STREAK", "Chuỗi ngày học"},
    {"STATS_ANS_FMT", "%d/%d đúng"},
    {"STATS_DAYS_FMT", "%d ngày"},
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
