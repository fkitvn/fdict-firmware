/* Cai dat > Thong tin phien ban (khong header, nen sang). Mot cot duy nhat, cuon doc, canh giua:
 *   FDICT-1.0.3          (Montserrat 32)   tieu de = FD_FW_NAME (= PROJECT_VER), 1 dong
 *   (neu ten co "-FW-" thi chia 2 dong; ten hien tai khong co)
 *   bang nhan | gia tri (toan chieu rong, nhan 96px xam nhat, hang >= 24px, ke manh)
 * FD_FW_NAME duoc dinh nghia luc build tu PROJECT_VER (CMakeLists.txt goc). */
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "fd_store.h"
#include "fd_tts.h"
#include "fd_display.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "ui_priv.h"

#ifndef FD_FW_NAME
#error "FD_FW_NAME chua duoc dinh nghia (xem CMakeLists.txt goc)"
#endif

#define ROW_H   24
#define LABEL_W 96

enum { R_FW, R_DEVID, R_BUILD, R_DATE, R_SIZE, R_FLASH, R_DATA, R_VOICE, R_IDF, R_MAC, R_IP, R_CHIP, R_RAM, R_LCD, R_OTA, R_COUNT };
static const char *const LABELS[R_COUNT] = {"Firmware", "Mã thiết bị", "Build", "Ngày build", "K\xC3\xAD" "ch th\xC6\xB0\xE1\xBB\x9B" "c",
                                            "B\xE1\xBB\x99 nh\xE1\xBB\x9B flash", "D\xE1\xBB\xAF li\xE1\xBB\x87u", "Gi\xE1\xBB\x8Dng \xC4\x91\xE1\xBB\x8D" "c", "ESP-IDF", "MAC",
                                            "IP", "Chip", "RAM trống", "Màn hình", "Máy chủ"};
/* Thu tu hien thi theo nhom (so am = tieu de nhom SECT[-n-1]) */
static const char *const SECT[] = {"Thi\xE1\xBA\xBFt b\xE1\xBB\x8B", "Ph\xE1\xBA\xA7n m\xE1\xBB\x81m", "B\xE1\xBB\x99 nh\xE1\xBB\x9B", "K\xE1\xBA\xBFt n\xE1\xBB\x91i"};
static const int8_t ORDER[] = {-1, R_CHIP, R_DEVID, R_MAC, R_LCD,
                               -2, R_FW, R_BUILD, R_DATE, R_SIZE, R_IDF, R_VOICE,
                               -3, R_FLASH, R_DATA, R_RAM,
                               -4, R_IP, R_OTA};
static lv_obj_t *s_val[R_COUNT];
static lv_obj_t *s_ota_msg, *s_ota_spin;
static lv_timer_t *s_ota_timer;

/* ---------- Kiem tra cap nhat (fd_ota: GET /ota/check, tai esp_https_ota) ---------- */
static lv_obj_t *s_ota_btn_lbl;

static const char *ota_err_vi(const char *e, char *buf, size_t n)
{
    if (!strcmp(e, "no_wifi")) return "chưa kết nối WiFi";
    if (!strcmp(e, "no_time")) return "chưa đồng bộ giờ";
    if (!strcmp(e, "batt_low")) return "pin yếu";
    if (!strcmp(e, "too_large")) return "bản quá lớn";
    if (!strcmp(e, "net")) return "mạng";
    if (!strcmp(e, "verify_failed")) return "sai mã kiểm tra";
    if (!strcmp(e, "bad_image")) return "ảnh không hợp lệ";
    if (!strncmp(e, "http_", 5)) { snprintf(buf, n, "máy chủ (%s)", e + 5); return buf; }
    return e;
}

static void ota_show(void)
{
    if (!s_ota_msg) return;
    fd_ota_status_t st;
    fd_ota_get_status(&st);
    char b[96], eb[40];
    bool spin = false;
    uint32_t col = 0x6B7280;
    const char *btn = "Kiểm tra cập nhật";
    bool busy = st.state == FD_OTA_ST_CHECKING || st.state == FD_OTA_ST_DOWNLOADING || st.state == FD_OTA_ST_REBOOT_PENDING;
    if (!busy && !fd_net_is_connected()) {
        lv_obj_set_hidden(s_ota_msg, false);
        lv_obj_set_hidden(s_ota_spin, true);
        if (strcmp(lv_label_get_text(s_ota_msg), "Cần Wi-Fi để kiểm tra cập nhật")) lv_label_set_text(s_ota_msg, "Cần Wi-Fi để kiểm tra cập nhật");
        lv_obj_set_style_text_color(s_ota_msg, lv_color_hex(0xDC2626), 0);
        if (strcmp(lv_label_get_text(s_ota_btn_lbl), btn)) lv_label_set_text(s_ota_btn_lbl, btn);
        return;
    }
    switch (st.state) {
    case FD_OTA_ST_IDLE:
        lv_obj_set_hidden(s_ota_msg, true);
        lv_obj_set_hidden(s_ota_spin, true);
        lv_label_set_text(s_ota_btn_lbl, btn);
        return;
    case FD_OTA_ST_CHECKING: spin = true; snprintf(b, sizeof(b), "Đang kiểm tra…"); break;
    case FD_OTA_ST_UP_TO_DATE: col = 0x16A34A; snprintf(b, sizeof(b), "Kh\xC3\xB4ng c\xC3\xB3 b\xE1\xBA\xA3n n\xC3\xA2ng c\xE1\xBA\xA5p"); break;
    case FD_OTA_ST_AVAILABLE: {
        unsigned c = (unsigned)(((uint64_t)st.size * 100 + 524288) / 1048576);   /* MB x100, lam tron */
        col = 0x2563EB;
        snprintf(b, sizeof(b), "Có bản mới %s (%u.%02u MB)", st.version, c / 100, c % 100);
        btn = "Cập nhật";
        break;
    }
    case FD_OTA_ST_DOWNLOADING: spin = true; snprintf(b, sizeof(b), "Đang tải %d%%", st.pct); break;
    case FD_OTA_ST_REBOOT_PENDING: col = 0xD97706; snprintf(b, sizeof(b), "Sẽ khởi động lại khi rảnh"); btn = "Khởi động lại ngay"; break;
    default: col = 0xDC2626; snprintf(b, sizeof(b), "Lỗi: %s", ota_err_vi(st.err, eb, sizeof(eb))); break;
    }
    lv_obj_set_hidden(s_ota_msg, false);
    lv_obj_set_hidden(s_ota_spin, !spin);
    if (strcmp(lv_label_get_text(s_ota_msg), b)) lv_label_set_text(s_ota_msg, b);
    lv_obj_set_style_text_color(s_ota_msg, lv_color_hex(col), 0);
    if (strcmp(lv_label_get_text(s_ota_btn_lbl), btn)) lv_label_set_text(s_ota_btn_lbl, btn);
}


/* ---------- Tu kiem tra khi mo man + hop thoai xac nhan nang cap (lv_layer_top) ---------- */
#define AUTO_CHECK_DEBOUNCE_US (60LL * 1000000)
static int64_t s_last_check_us;   /* giu qua cac lan mo man: mo lai trong 60 s -> dung ket qua cu */
static bool s_auto_wait;          /* cho WiFi de tu kiem tra */
static bool s_popup_pending;      /* hien hop thoai khi co ban moi (1 lan / lan mo man) */
static lv_obj_t *s_dlg;

static void dlg_close(void)
{
    if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }
}

static void dlg_btn_cb(lv_event_t *e)
{
    bool go = (bool)(intptr_t)lv_event_get_user_data(e);
    dlg_close();
    if (go) {
        fd_ota_status_t st;
        fd_ota_get_status(&st);
        if (st.state == FD_OTA_ST_AVAILABLE) fd_ota_update_now();   /* duong tai thu cong co san */
    }
}

static void dlg_btn(lv_obj_t *row, const char *txt, bool primary)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, dlg_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)primary);
}

static void dlg_open(const fd_ota_status_t *st)
{
    if (s_dlg) return;
    s_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dlg);
    lv_obj_set_size(s_dlg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dlg, LV_OPA_40, 0);
    lv_obj_set_clickable(s_dlg, true);
    lv_obj_t *box = lv_obj_create(s_dlg);
    lv_obj_set_size(box, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 14, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_set_style_text_font(box, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(box, lv_color_hex(0x111827), 0);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    unsigned c = (unsigned)(((uint64_t)st->size * 100 + 524288) / 1048576);   /* MB x100, lam tron */
    lv_obj_t *t = lv_label_create(box);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_label_set_text_fmt(t, "Có phiên bản mới %s/%s (%u.%02u MB). Nâng cấp ngay?", st->version, st->build, c / 100, c % 100);
    if (st->notes[0]) {
        lv_obj_t *n = lv_label_create(box);
        lv_obj_set_width(n, lv_pct(100));
        lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(n, lv_color_hex(0x6B7280), 0);
        lv_label_set_text(n, st->notes);
    }
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    dlg_btn(row, "Để sau", false);
    dlg_btn(row, "Nâng cấp", true);
}

static void auto_check(void)   /* khi mo man / khi WiFi vua co */
{
    fd_ota_status_t st;
    fd_ota_get_status(&st);
    if (st.state == FD_OTA_ST_CHECKING || st.state == FD_OTA_ST_DOWNLOADING || st.state == FD_OTA_ST_REBOOT_PENDING) {
        s_auto_wait = false;
        return;
    }
    if (!fd_net_is_connected()) { s_auto_wait = true; return; }
    s_auto_wait = false;
    int64_t now = esp_timer_get_time();
    if (s_last_check_us && now - s_last_check_us < AUTO_CHECK_DEBOUNCE_US && st.state != FD_OTA_ST_IDLE) return;   /* dung ket qua cu */
    s_last_check_us = now;
    fd_ota_check_now();   /* duong kiem tra thu cong co san */
}

static void ota_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_auto_wait && fd_net_is_connected()) auto_check();
    ota_show();
    if (!s_popup_pending) return;
    fd_ota_status_t st;
    fd_ota_get_status(&st);
    if (st.state == FD_OTA_ST_AVAILABLE) { s_popup_pending = false; dlg_open(&st); }
    else if (st.state == FD_OTA_ST_UP_TO_DATE || st.state == FD_OTA_ST_ERROR || st.state == FD_OTA_ST_REBOOT_PENDING) s_popup_pending = false;
}

static bool on_back(void)
{
    if (!s_dlg) return false;
    dlg_close();
    return true;
}

static void ota_cb(lv_event_t *e)
{
    (void)e;
    fd_ota_status_t st;
    fd_ota_get_status(&st);
    if (st.state == FD_OTA_ST_REBOOT_PENDING) { fd_ota_reboot_now(); return; }   /* da tai xong: khoi dong lai ngay */
    if (st.state == FD_OTA_ST_CHECKING || st.state == FD_OTA_ST_DOWNLOADING) return;
    if (st.state == FD_OTA_ST_AVAILABLE) fd_ota_update_now();
    else if (fd_net_is_connected()) { s_last_check_us = esp_timer_get_time(); s_popup_pending = true; fd_ota_check_now(); }
}

/* Kich thuoc dang "12 KB" / "6,1 MB" (dau phay thap phan; dec = so chu so sau dau phay khi >= 1 MB) */
static void fmt_sz(char *out, size_t n, uint64_t v, int dec)
{
    if (v < 1024u * 1024u) { snprintf(out, n, "%u KB", (unsigned)((v + 512) / 1024)); return; }
    unsigned k = dec >= 2 ? 100 : 10;
    unsigned x = (unsigned)((v * k + 512u * 1024u) / (1024u * 1024u));
    if (dec >= 2) snprintf(out, n, "%u,%02u MB", x / 100, x % 100);
    else snprintf(out, n, "%u,%u MB", x / 10, x % 10);
}

/* Flash / /data / giong doc: tinh khi mo man (esp_littlefs_info duyet FS - khong goi moi tick) */
static void mem_rows(void)
{
    char a[16], b[16], o[64];
    uint32_t fl = 0;
    esp_flash_get_size(NULL, &fl);
    lv_label_set_text_fmt(s_val[R_FLASH], "%u MB", (unsigned)(fl >> 20));
    size_t tot = 0, used = 0;
    if (fd_store_info(&tot, &used) == ESP_OK && tot) {
        unsigned p10 = (unsigned)(((uint64_t)used * 1000 + tot / 2) / tot);
        fmt_sz(a, sizeof(a), used, 1);
        fmt_sz(b, sizeof(b), tot, 1);
        snprintf(o, sizeof(o), "%s / %s (%u,%u%%)", a, b, p10 / 10, p10 % 10);
    } else snprintf(o, sizeof(o), "Kh\xC3\xB4ng \xC4\x91\xE1\xBB\x8D" "c \xC4\x91\xC6\xB0\xE1\xBB\xA3" "c");
    lv_label_set_text(s_val[R_DATA], o);
    char vo[48];
    const esp_partition_t *vp = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "voice");
    uint32_t h[8] = {0};   /* header voice.bin (tools/mk_voice.py): magic[8], ver, ta_off, ta_len, sg_off, sg_len, crc */
    if (vp && fd_tts_voice_ok() && esp_partition_read(vp, 0, h, sizeof(h)) == ESP_OK) {
        fmt_sz(a, sizeof(a), (uint64_t)h[5] + h[6], 2);
        snprintf(vo, sizeof(vo), "%s \xE2\x80\x93 OK", a);
    } else snprintf(vo, sizeof(vo), "Thi\xE1\xBA\xBFu");
    lv_label_set_text(s_val[R_VOICE], vo);
    ESP_LOGD("scr_about", "bo nho: flash %u MB | data %u/%u B -> %s | giong %s | RAM noi bo free %u B, PSRAM %u B (free %u B)",
             (unsigned)(fl >> 20), (unsigned)used, (unsigned)tot, o, vo, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)(esp_psram_is_initialized() ? esp_psram_get_size() : 0), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* Kich thuoc anh firmware dang chay (tinh 1 lan, cache): image_len tu esp_image_get_metadata / dung luong phan vung */
static void fw_size_text(char *out, size_t n)
{
    static char s_txt[48];
    if (!s_txt[0]) {
        const esp_partition_t *p = esp_ota_get_running_partition();
        esp_image_metadata_t md = {0};
        const esp_partition_pos_t pos = {.offset = p ? p->address : 0, .size = p ? p->size : 0};
        if (p && esp_image_get_metadata(&pos, &md) == ESP_OK && md.image_len > 0 && p->size > 0) {
            unsigned len = (unsigned)md.image_len, ps = (unsigned)p->size;
            unsigned mb100 = (unsigned)(((uint64_t)len * 100 + 512u * 1024u) / (1024u * 1024u));
            unsigned pct10 = (unsigned)(((uint64_t)len * 1000 + ps / 2) / ps);
            snprintf(s_txt, sizeof(s_txt), "%u,%02u MB (%u,%u%% / %u MB)", mb100 / 100, mb100 % 100, pct10 / 10, pct10 % 10, ps >> 20);
            ESP_LOGD("scr_about", "fw size: %u B / phan vung %s %u B -> %s", len, p->label, ps, s_txt);
        } else {
            snprintf(s_txt, sizeof(s_txt), "--");
            ESP_LOGI("scr_about", "fw size: khong doc duoc metadata");
        }
    }
    snprintf(out, n, "%s", s_txt);
}

static void refresh(void)
{
    if (!s_val[0]) return;
    const esp_app_desc_t *d = esp_app_get_description();
    lv_label_set_text(s_val[R_FW], FD_FW_NAME);
    {
        char id[40];
        ui_device_ident(id, sizeof(id));
        lv_label_set_text(s_val[R_DEVID], id);
    }
    char sha[9];
    esp_app_get_elf_sha256(sha, sizeof(sha));
    lv_label_set_text_fmt(s_val[R_BUILD], FD_BUILD_PREFIX "%.4s", sha);   /* fdictdm10 + 4 hex SHA256 ELF */
    lv_label_set_text_fmt(s_val[R_DATE], "%s %s", d->date, d->time);
    {
        char fs[48];
        fw_size_text(fs, sizeof(fs));
        lv_label_set_text(s_val[R_SIZE], fs);
    }
    lv_label_set_text(s_val[R_IDF], esp_get_idf_version());
    {   /* Chip: model, revision, so nhan, "N<flash MB>R<PSRAM MB>" tinh tu kich thuoc thuc (luc chay) */
        esp_chip_info_t ci;
        esp_chip_info(&ci);
        uint32_t fl = 0;
        esp_flash_get_size(NULL, &fl);
        unsigned ps = esp_psram_is_initialized() ? (unsigned)(esp_psram_get_size() >> 20) : 0;
        char cb[48];
        snprintf(cb, sizeof(cb), "%s v%u.%u, %u nh\u00e2n, N%uR%u", ci.model == CHIP_ESP32S3 ? "ESP32-S3" : "ESP32",
                 ci.revision / 100, ci.revision % 100, ci.cores, (unsigned)(fl >> 20), ps);
        lv_label_set_text(s_val[R_CHIP], cb);
    }
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    lv_label_set_text_fmt(s_val[R_MAC], "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    char ip[16] = "";
    fd_net_ip(ip, sizeof(ip));
    lv_label_set_text(s_val[R_IP], (fd_net_is_connected() && ip[0]) ? ip : "Chưa kết nối");
    {   /* RAM trong: noi bo + PSRAM (chi khi co) */
        char ri[16], rp[16];
        fmt_sz(ri, sizeof(ri), heap_caps_get_free_size(MALLOC_CAP_INTERNAL), 1);
        if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) {
            fmt_sz(rp, sizeof(rp), heap_caps_get_free_size(MALLOC_CAP_SPIRAM), 1);
            lv_label_set_text_fmt(s_val[R_RAM], "N\xE1\xBB\x99i b\xE1\xBB\x99 %s \xC2\xB7 PSRAM %s", ri, rp);
        } else lv_label_set_text_fmt(s_val[R_RAM], "N\xE1\xBB\x99i b\xE1\xBB\x99 %s", ri);
    }
    {
        char base[64];
        bool lab = false;
        fd_ota_get_base(base, sizeof(base), &lab);
        lv_label_set_text_fmt(s_val[R_OTA], "%s%s", base, lab ? " (Lab)" : " (Chính thức)");
    }
    lv_label_set_text_fmt(s_val[R_LCD], "%dx%d, xoay %d°", (int)lv_display_get_horizontal_resolution(NULL),
                          (int)lv_display_get_vertical_resolution(NULL),
                          fd_display_rotation_deg(fd_display_get_rotation()));
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *col = ui_page_create(scr);   /* flex column toan man */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0x111827), 0);
    lv_obj_set_style_pad_hor(col, 14, 0);
    lv_obj_set_style_pad_ver(col, 8, 0);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(col, true);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);   /* chi cuon doc: khong tranh vuot mep ngang */
    lv_obj_set_style_width(col, 3, LV_PART_SCROLLBAR);   /* thanh cuon manh, xam nhat */
    lv_obj_set_style_bg_color(col, lv_color_hex(0x9CA3AF), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(col, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(col, 2, LV_PART_SCROLLBAR);

    /* Tieu de: FD_FW_NAME chia tai "-FW-" -> "FDICT-Fkit" / "FW-1.0" */
    const char *fw = strstr(FD_FW_NAME, "-FW-");
    lv_obj_t *t1 = lv_label_create(col);
    lv_obj_set_style_text_font(t1, &fd_font_mont_32, 0);
    if (fw) {
        lv_label_set_text_fmt(t1, "%.*s", (int)(fw - FD_FW_NAME), FD_FW_NAME);
        lv_obj_t *t2 = lv_label_create(col);
        lv_obj_set_style_text_font(t2, &fd_font_noto_18, 0);
        lv_obj_set_style_text_color(t2, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
        lv_label_set_text(t2, fw + 1);
    } else {
        lv_obj_set_width(t1, lv_pct(100));
        lv_obj_set_style_text_align(t1, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(t1, LV_LABEL_LONG_WRAP);
        lv_label_set_text(t1, FD_FW_NAME);
    }


    lv_obj_t *ob = lv_button_create(col);
    lv_obj_set_size(ob, lv_pct(100), 42);
    lv_obj_set_style_radius(ob, 10, 0);
    lv_obj_set_style_shadow_width(ob, 0, 0);
    lv_obj_set_style_bg_color(ob, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_t *ol = lv_label_create(ob);
    lv_obj_set_style_text_color(ol, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_obj_set_width(ol, lv_pct(100)); lv_label_set_long_mode(ol, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(ol, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(ol, "Kiểm tra cập nhật");
    lv_obj_center(ol);
    s_ota_btn_lbl = ol;
    lv_obj_add_event_cb(ob, ota_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *orow = lv_obj_create(col);
    lv_obj_remove_style_all(orow);
    lv_obj_set_size(orow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(orow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(orow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(orow, 6, 0);
    s_ota_spin = lv_spinner_create(orow);
    lv_obj_set_size(s_ota_spin, 18, 18);
    lv_obj_set_style_arc_width(s_ota_spin, 3, 0);
    lv_obj_set_style_arc_width(s_ota_spin, 3, LV_PART_INDICATOR);
    lv_obj_set_hidden(s_ota_spin, true);
    s_ota_msg = lv_label_create(orow);
    lv_obj_set_style_max_width(s_ota_msg, 190, 0);
    lv_label_set_long_mode(s_ota_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_ota_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_hidden(s_ota_msg, true);
    ota_show();

    s_popup_pending = true;
    auto_check();
    s_ota_timer = lv_timer_create(ota_timer_cb, 300, NULL);   /* trang thai / % tai */

    for (size_t k = 0; k < sizeof(ORDER) / sizeof(ORDER[0]); k++) {
        int i = ORDER[k];
        if (i < 0) {   /* tieu de nhom: chu nho, mau nhat */
            lv_obj_t *h = lv_label_create(col);
            lv_obj_set_width(h, lv_pct(100));
            lv_obj_set_style_text_font(h, &fd_font_noto_14, 0);
            lv_obj_set_style_text_color(h, lv_color_hex(0x9CA3AF), 0);
            lv_obj_set_style_pad_top(h, k ? 8 : 2, 0);
            lv_label_set_text(h, SECT[-i - 1]);
            continue;
        }
        lv_obj_t *row = lv_obj_create(col);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, ROW_H, 0);
        lv_obj_set_style_pad_ver(row, 3, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_clickable(row, false);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0xE5E7EB), 0);
        lv_obj_t *l = lv_label_create(row);
        lv_obj_set_width(l, LABEL_W);
        lv_obj_set_style_text_color(l, lv_color_hex(0x6B7280), 0);
        lv_label_set_text(l, LABELS[i]);
        s_val[i] = lv_label_create(row);
        lv_obj_set_flex_grow(s_val[i], 1);
        lv_label_set_long_mode(s_val[i], LV_LABEL_LONG_WRAP);
    }

    refresh();
    mem_rows();
}

static void destroy(void)
{
    for (int i = 0; i < R_COUNT; i++) s_val[i] = NULL;
    if (s_ota_timer) {
        lv_timer_delete(s_ota_timer);
        s_ota_timer = NULL;
    }
    s_ota_msg = s_ota_spin = s_ota_btn_lbl = NULL;
    dlg_close();
    s_auto_wait = s_popup_pending = false;
}
static void on_tick(void) { refresh(); }
static void on_event(int32_t id, void *data) { (void)id; (void)data; refresh(); }

const ui_screen_t g_scr_about = {
    .name = "about", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy, .on_event = on_event,
    .on_tick = on_tick, .on_back = on_back,
};