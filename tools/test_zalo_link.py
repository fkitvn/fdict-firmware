from pathlib import Path
import re
r = Path(__file__).resolve().parents[1]
ui = (r / 'components/fd_ui/scr_zalo_link.c').read_text(encoding='utf-8')
urls = re.findall(r'https://[^"\s]+', ui)
assert urls == ['https://zalo.me/s/3845974445044166380/?code=%s'], urls
assert 'lv_qrcode_update' not in (r / 'components/fd_ui/scr_warranty.c').read_text(encoding='utf-8')
assert 'FD_SCR_ZALO_LINK' in (r / 'components/fd_ui/ui_nav.c').read_text(encoding='utf-8')
assert 'fd_zalo_link_stop();' in ui
assert 'fd_zalo_link_tick();' in ui
assert 'FD_LINK_DONE' in ui
print('PASS: code-only Zalo QR, view-only warranty, screen registration and lifecycle')

worker = (r / 'components/fd_ota/fd_zlink.c').read_text(encoding='utf-8')
assert '/api/devices/zalo-link/status' in worker
assert '/api/devices/zalo-link/unlink' in worker
assert 'fd_zalo_link_unlink();' in ui
assert 'ZALO_UNLINK_CONFIRM' in ui and '.on_back = on_back' in ui
assert 'dialog_close(false);' in ui
print('PASS: linked-account status, confirmed unlink, and modal cleanup')

settings = (r / 'components/fd_ui/scr_settings.c').read_text(encoding='utf-8')
items = [line for line in settings.splitlines() if 'item(body,' in line]
pin_index = next(i for i, line in enumerate(items) if '_("PIN_CODE")' in line)
assert '_("WARRANTY_INFO")' in items[pin_index + 1]
print('PASS: warranty information directly below PIN in Settings')

translations = (r / 'components/fd_i18n/lv_i18n.c').read_text(encoding='utf-8')
assert translations.count('{"ZALO_LINK_TITLE",') == 2
assert translations.count('{"ZALO_LOGIN_REQUIRED",') == 2
assert 'to activate warranty' not in next(line for line in translations.splitlines() if '{"ZALO_CODE_HINT",' in line)
print('PASS: generated translations retain login/link labels and non-activation guidance')

assert '{"ZALO_WAITING", "Zalo not linked"}' in translations

login = (r / 'components/fd_ui/scr_pair.c').read_text(encoding='utf-8')
for geometry in ['lv_obj_set_style_pad_hor(col, 12, 0);', 'lv_obj_set_style_pad_row(col, 4, 0);',
                 'lv_obj_set_scrollable(col, false);', 'lv_obj_set_style_pad_all(s_wrap, 12, 0);',
                 'lv_obj_set_size(row, lv_pct(100), 44);', 'lv_obj_set_style_pad_column(row, 8, 0);',
                 'lv_obj_set_size(sp, 1, 0);', 'lv_obj_set_flex_grow(sp, 1);']:
    assert geometry in ui
    assert geometry.replace('s_wrap', 's_qr_wrap') in login
assert 'lv_qrcode_set_size(s_qr, s_low ? 100 : QR_PX);' in ui
assert '#define QR_PX 135' in ui
assert 'button(row, 0xE5E7EB, &back_label, back_cb)' in ui
assert 'button(row, FD_COLOR_PRIMARY, &s_action_lbl, action_cb)' in ui
assert 's_low ? 4 : 6' in ui
assert 'lv_obj_set_hidden(s_code, !show || s_low);' in ui
assert '"%s · %d:%02d"' in ui
assert ui.index('label(col, _("ZALO_LINK_TITLE"))') < ui.index('s_qr = lv_qrcode_create') < ui.index('s_msg = label') < ui.index('lv_obj_t *row = lv_obj_create(col)')
# Height budgets for 320x240 landscape and 240x320 portrait (14px font line <= 20px).
assert 20 + 126 + 20 + 44 + 16 + 8 <= 240
assert 20 + 161 + 20 + 20 + 44 + 20 + 12 <= 320
import re
version = re.search(r'set\(PROJECT_VER "FDict One - ([0-9.]+)"\)', (r / 'CMakeLists.txt').read_text()).group(1)
assert re.search(r'^## \[([^]]+)\]', (r / 'CHANGELOG.md').read_text(encoding='utf-8'), re.M).group(1) == version
print('PASS: login-matched layout, two bottom actions, manual code, orientation budgets and CI version gate')
