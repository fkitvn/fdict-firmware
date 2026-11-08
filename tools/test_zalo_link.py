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
