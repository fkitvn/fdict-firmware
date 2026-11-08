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
