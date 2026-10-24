#pragma once
/* Vung bat dau vuot tu mep (px, toa do logic sau xoay). Ap dung giong het cho MOI man (cai dat toan thiet bi).
 * Lich su (2026-10-18): 26 -> 38 -> 48 -> 72 -> 96 -> 80 -> 72 -> 64; tu 10:43 trai/phai 72, tren/duoi 52; tu 16:35 trai/phai 64, tren/duoi 48; 16:38 duoi 44 (320x240: giua con 192x148). Cham khong di chuyen (< FD_SWIPE_MIN_PX) van la tap cho widget. */
#define FD_SWIPE_EDGE_PX    52   /* mep trai/phai: vuot vao -> quay lai */
#define FD_SWIPE_TOP_PX     40   /* mep tren: keo xuong -> mo bang nhanh */
#define FD_SWIPE_BOTTOM_PX  0    /* mep duoi: TAT vuot len -> Home (2026-10-21 10:37). 0 = khong co vung mep duoi (y kep <= h-1 < h) */
#define FD_SWIPE_TOL_PX     2    /* dung sai: diem cham lech ra ngoai vung hien thi 1-2 px van tinh (kep ve 0..max) */
#define FD_SWIPE_MIN_PX     48   /* quang duong toi thieu cho MOI cu chi (quay lai, keo xuong, vuot len, dong bang nhanh); 30 -> 60 -> 48 (2026-10-18) */
/* Huong: chi can truc chinh troi hon (|dx| > |dy| cho vuot ngang, nguoc lai cho vuot doc), ~45 do.
 * O goc (vua trong vung mep tren/duoi vua trong vung mep trai/phai) huong vuot quyet dinh cu chi. */
/* Vuot ngang o BAT KY DAU (khong can bat dau trong vung mep) = quay lai, neu |dx| > |dy| va |dx| >= FD_SWIPE_MIN_PX.
 * Ngoai le (toan thiet bi): bat dau tren slider/bar/arc/roller/switch/textarea/keyboard/buttonmatrix/dropdown/spinbox,
 * hoac doi tuong (hay cha) cuon ngang that su; hoac khi bang nhanh dang mo. Mep tren/duoi van chi tu vung mep. */
#define FD_SWIPE_TAP_SLOP_PX 10   /* bat dau trong vung mep + di > 10 px dung huong cu chi -> khong con la cham: huy nut (PRESS_LOST), khong CLICKED/LONG_PRESSED */
#define FD_SWIPE_BACK_ANYWHERE 0   /* TAT (10:09): chi vuot bat dau trong vung mep trai/phai moi la quay lai; ngoai le van ap dung */
