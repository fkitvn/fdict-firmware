# Changelog

Định dạng theo [Keep a Changelog](https://keepachangelog.com/), phiên bản theo [SemVer](https://semver.org/).
Build ID (màn *Thông tin*) = 10 ký tự hex đầu (chữ thường) SHA256 của file ELF.

## [2.2.5] - 2026-11-05

- Thử 4 nút bấm: chỉ hiện Nút 1–4 (trái → phải), bớt chữ.
- Sửa lỗi bấm một nút bị ghi nhận thêm nút IO21: bỏ qua lần nhấn "ma" xảy ra gần như cùng lúc với nút khác; log mức GPIO cả 4 nút để chẩn đoán.

## [2.2.4] - 2026-11-05

- Cài đặt: mục đổi tên "Dành cho nhà phát triển" (công tắc bên trong vẫn là "Chế độ nhà phát triển").
- Gỡ bỏ hoàn toàn tính năng Shadowing (ô Home, màn hình, icon, chuỗi, API luồng video).

## [2.2.3] - 2026-11-05

- Thử 4 nút bấm: 4 ô một hàng ở đáy màn hình, đúng vị trí nút thật (IO2, IO3, IO14, IO21).
- Thử Wi-Fi: giao diện mới (vạch tín hiệu, vòng tiến độ 60 s, thẻ số liệu, kết luận màu, RSSI min/TB/max).
- Thử micro chuyển vào Chế độ nhà phát triển, đổi tên "Thử loa & micro", thêm thử loa (âm 1 kHz, quét tần số).

## [2.2.2] - 2026-11-05

- Cài đặt > Chế độ nhà phát triển: màn hình riêng gồm công tắc bật/tắt, Thử 4 nút bấm (bấm lần lượt B1–B4, Đạt khi đủ 4) và Thử Wi-Fi (SSID, RSSI, IP; 60 s gửi HTTP tới máy chủ, đếm thành công/lỗi, độ trễ TB/max, số lần mất kết nối).

## [2.2.1] - 2026-11-05

- Phát hành lại đầy đủ thay đổi 2.2.0; icon đen-trắng (tương phản) trên bảng nhanh.

## [2.2.0] - 2026-11-05

- Wi-Fi: tăng bộ đệm RX (ổn định hơn khi OTA).
- Logo FDict chỉ hiện lúc khởi động / bật từ nút Ngủ.
- Sắp xếp câu: tùy chọn phát âm sau mỗi câu trước khi Bắt đầu.
- Bảng nhanh: icon đen-trắng đổi sang biểu tượng tương phản (nửa vòng).

## [2.1.9] - 2026-11-05

- Sửa rè/méo loa ở mức âm lượng cao: âm lượng tối đa giờ không vượt mức gốc của bộ giải mã (0 dB).
- Thanh âm lượng chỉnh đều tai hơn (giảm dần theo dB): 100% to nhất không méo, 50% khoảng -20 dB.
- Mức âm lượng ở 70% trở xuống sẽ nhỏ hơn trước; tăng lên nếu cần.
- Bảng kéo xuống: nút Home thay bằng nút bật/tắt màn hình đen trắng.

## [2.1.8] - 2026-11-05

- Thêm màn "Thử micro" (Cài đặt): ghi âm 5 giây từ micro trên máy, hiện thời lượng và mức âm, nghe lại qua loa hoặc xóa bản ghi.
- Bản ghi lưu trong bộ nhớ máy (một file, ghi đè lần trước), kiểm tra dung lượng trống trước khi ghi.
- Khi nghe lại, âm lượng được giữ ở mức vừa phải để tránh rè.

## [2.1.7] - 2026-11-05

- Sắp xếp câu: mỗi từ là một thẻ, chạm lần lượt để ghép câu, đủ từ là tự chấm ngay; chạm vào câu để xóa từ vừa chọn.
- Học được khi không có mạng: dùng bộ câu đã tải hoặc câu mẫu có sẵn, chấm ngay trên máy; kết quả tự đồng bộ khi có mạng trở lại.
- Ô "Sắp xếp câu" có biểu tượng riêng (mảnh ghép).

## [2.1.6] - 2026-11-04

- Thêm bài học mới "Sắp xếp câu": xếp các thẻ từ đã xáo trộn thành câu tiếng Anh hoàn chỉnh bằng nút bấm hoặc chạm, kiểm tra đáp án ngay, xem câu đúng và nghĩa tiếng Việt. Câu lấy từ ví dụ trong bộ từ của bạn, chưa có thì dùng câu mẫu.

## [2.1.5] - 2026-11-04

- Thêm chế độ đen trắng trong Cài đặt.

## [2.1.4] - 2026-11-04

- Kiểm tra cập nhật ổn định hơn: tự thử kết nối lại khi mạng chập chờn; chỉ kiểm tra khi mở màn Thông tin; tải xong tự khởi động lại.

## [2.1.3] - 2026-11-03

- Flashcard và Quiz học cả từ quét bài học / bộ từ trên Mini App.

## [2.1.2] - 2026-11-02

- Bước 2 hiện mã kích hoạt bảo hành nhanh hơn: xin mã ngay (bỏ lần hỏi trạng thái trước), dùng lại mã còn hạn khi mở lại màn.

## [2.1.1] - 2026-11-02

- Man Ghep chi con 2 buoc: Buoc 1 Dinh danh, Buoc 2 Kich hoat bao hanh (thay Lien ket Zalo). Nhap ma 6 so trong Mini App = lien ket Zalo + kich hoat bao hanh.
- Buoc 2 xong khi server bao bao hanh da kich hoat (zalo-link/status warranty_activated, ban ghi bao hanh rieng theo thiet bi). Bo khoi Buoc 3.

## [2.1.0] - 2026-11-02

- Màn *Ghép thiết bị*: Bước 3 *Kích hoạt bảo hành*. Khi đã liên kết Zalo mà chưa kích hoạt: "Bước 3: Kích hoạt bảo hành" + "Kích hoạt trong Zalo Mini App Fdict" (tự hỏi lại mỗi 10 s). Đã kích hoạt: "Đã kích hoạt bảo hành" + ngày hết hạn (dd/mm/yyyy, giờ VN).
- Trạng thái bảo hành đọc từ `POST /api/devices/zalo-link/status` (`warranty_activated`, `warranty_expires_at`). Không có thao tác kích hoạt trên máy (chỉ trong Zalo Mini App).
- Cài đặt: hàng Ghép thiết bị báo "Chưa kích hoạt bảo hành" (màu cảnh báo); *Thông tin* thêm hàng *Bảo hành*.

## [2.0.1] - 2026-11-02

- Home: thêm ô *Shadowing* (🎬) mở trình phát video toàn màn hình (ngang 320x240; máy đang dọc thì tạm xoay ngang, thoát trả lại hướng cũ, không lưu NVS).
- Video cố định `https://www.youtube.com/watch?v=Gp340_5dT5g`, phát qua server: `GET /api/devices/media/stream` (Bearer thiết bị, `fd_ota_base_url()`), luồng FDMS v1 = MJPEG 320x240 ~10 fps + PCM s16 mono 16 kHz.
- Giải mã JPEG bằng TJpgDec trong ROM vào 2 bộ đệm PSRAM; âm thanh qua ES8311 bằng API luồng mới `fd_audio_stream_*` (bộ đệm jitter 3 s, bắt đầu phát khi đủ 0,8 s). Đồng bộ: âm thanh là đồng hồ, khung trễ > 200 ms bị bỏ.
- Trạng thái: đang tải / chưa WiFi / chưa ghép / server bận (429/503) / lỗi mạng / hết video. Chạm hoặc vuốt mép để thoát.

## [2.0.0] - 2026-11-02

- Màn *Ghép thiết bị* thành 2 bước: thanh bước (1) Định danh → (2) Liên kết Zalo. Bước 1 giữ nguyên QR + web (Gmail/OTP) + poll.
- Định danh xong tự chuyển thẳng Bước 2: mã 6 số (5 phút, đếm ngược, "Lấy mã mới", "Để sau"), nhập trong Mini App FDict trên Zalo. Mở lại khi đã ghép nhưng chưa liên kết Zalo -> vào thẳng Bước 2. Đã liên kết -> 2 bước đều ✓ + tài khoản / Hủy ghép đôi như cũ.
- Cài đặt: bỏ hàng "Bước 2: Liên kết Zalo" của 1.3.7; thẻ ghép mở màn Ghép thiết bị, hiện "Chờ liên kết Zalo" khi đã ghép mà chưa liên kết.

## [1.3.7] - 2026-11-02

- Cài đặt: thêm hàng "Bước 2: Liên kết Zalo" dưới thẻ định danh (Bước 1). Chưa định danh thì mờ; chưa liên kết hiện "Chờ liên kết Zalo", đã liên kết hiện "Đã liên kết Zalo".
- Hộp mã 6 số (hết hạn 5 phút, đếm ngược, "Lấy mã mới", "Để sau"); nhập mã trong Mini App FDict trên Zalo. Thiết bị poll `/api/devices/zalo-link/poll` mỗi 4 s; mã không ghi log.
- Khởi động và khi mở Cài đặt: hỏi `/api/devices/zalo-link/status` (task nền `fd_zlink`).

## [1.3.6] - 2026-10-27

- Lab/dev: doi base URL sang `https://fdict-dev.joinnow.vn` (QR `/pairing-device`); HTTPS dung chung certificate bundle nhu production, khong tat kiem tra chung chi. Van cho phep `http://` Lab neu cau hinh lai thanh HTTP LAN.
- Che do nha phat trien bat van chon Lab qua `fd_ota_base_url()` / `fd_ota_pair_web_url()`; tat van dung `https://dict.fkit.vn`.

## [1.3.5] - 2026-10-26

- Cập nhật firmware: hạ ngưỡng pin tối thiểu để bắt đầu cập nhật từ 30% xuống 10%.

## [1.3.4] - 2026-10-26

- Cài đặt: chỉ báo chưa ghép thiết bị tinh tế hơn: bỏ nền và viền nổi bật, chỉ giữ chữ trạng thái màu cam và một chấm cam nhỏ ở góc biểu tượng.

## [1.3.3] - 2026-10-26

- Thông tin: dòng Chip chỉ hiện tên rút gọn (vd "ESP32-S3").
- Cài đặt: khi chưa ghép thiết bị, mục Ghép thiết bị được làm nổi bật (nền vàng nhạt, viền và chữ cam, biểu tượng cảnh báo).

## [1.3.2] - 2026-10-26

- Cài đặt: mục Ngôn ngữ nằm ngay dưới Wi-Fi, nhãn chỉ hiện theo ngôn ngữ hiện tại; chọn ngôn ngữ xong tự quay về Cài đặt.
- Dịch Việt/Anh: Ghép thiết bị, Mã PIN, hộp thoại Ngủ, bảng nhanh, màn chào, thông báo.
- Thông tin: RAM nội bộ và PSRAM hiển thị dạng "đã dùng / tổng (%)" như dòng Dữ liệu.

## [1.3.1] - 2026-10-26

- Học tập (Anh-Việt): khi bật Tự động phát (1 lần / 2 lần), máy tự đọc từ vựng mỗi khi chuyển sang câu hỏi mới, kể cả câu đầu tiên và câu sau khi tải thêm; mỗi câu chỉ đọc một lượt.

## [1.3.0] - 2026-10-26

- Giao diện song ngữ Tiếng Việt / English (lv_i18n): Trang chủ, Cài đặt, Thông tin, Wi-Fi, Học tập, Flash card và các màn cài đặt con.
- Cài đặt: thêm mục “Ngôn ngữ / Language” để chọn ngôn ngữ, lưu lại sau khi khởi động lại (mặc định Tiếng Việt).
- Màn Thông tin: RAM hiển thị dạng đã dùng (% / tổng), MAC chuyển sang nhóm Kết nối, giọng đọc ghi “(SVOX Pico)”, bỏ chữ OK.

## [1.2.7] - 2026-10-26

- Màn hình Thông tin phiên bản: tách RAM thành hai dòng RAM nội bộ và PSRAM, hiển thị đã dùng / tổng.

## [1.2.6] - 2026-10-26

- Màn hình Thông tin phiên bản: bỏ chữ "Chính thức" sau địa chỉ máy chủ.

## [1.2.5] - 2026-10-26

- Bản thử cập nhật OTA, không đổi tính năng.

## [1.2.4] - 2026-10-25

- Màn Thông tin: khi chưa kết nối Wi-Fi, bấm dòng “Cần Wi-Fi để kiểm tra cập nhật” (gạch chân) để mở nhanh màn cài đặt Wi-Fi.

## [1.2.3] - 2026-10-25

- Màn hình chào: chữ “FDict One - Máy học từ vựng” nhỏ hơn một cỡ (Noto 14), vẫn một dòng, căn giữa dưới logo.

## [1.2.2] - 2026-10-25

- Bản kiểm thử cập nhật OTA từ 1.2.1, không đổi tính năng.
- Log khởi động ghi rõ build_id và độ dài (10 ký tự).

## [1.2.1] - 2026-10-25

- Sửa mã bản dựng chỉ có 9 ký tự (máy chủ báo lỗi 400): nay đủ 10 ký tự hex đầu SHA256 của ELF.
- Thêm DNS dự phòng khi Wi-Fi không phân giải được tên máy chủ (lỗi: mạng).
- Máy đang chạy 1.1.9/1.2.0 cần nạp lại qua USB.

## [1.2.0] - 2026-10-25

- Màn hình chào: dòng chữ “FDict One - Máy học từ vựng” dưới logo hiển thị trên một dòng, không còn bị xuống dòng.

## [1.1.9] - 2026-10-25

- Màn hình chào hiển thị logo và dòng “FDict One - Máy học từ vựng”.
- Thống nhất tên gọi: thiết bị là FDict One, phần mềm máy tính là FDict (menu cấu hình, tài liệu, thông báo).

## [1.1.8] - 2026-10-25

- Đổi tên hiển thị của máy thành FDict One; phần mềm máy tính tên là FDict.
- Phiên bản chỉ còn dạng “FDict One - x.y.z” (màn Thông tin hiển thị đúng dạng này); màn khởi động chỉ còn logo và chữ FDict One.
- Mã build mới: 10 ký tự hex đầu của SHA256 file ELF.
- Máy đang chạy bản cũ (FDICT-x.y.z) cần nạp lại qua USB.

## [1.1.7] - 2026-10-25

- Đổi định dạng mã build: bỏ tiền tố fdictdm, chỉ còn 2 chữ số + 4 ký tự hex (ví dụ 10a3f9).
- Đổi tên hiển thị của máy thành Fdict One.
- Nạp firmware trực tiếp qua USB từ trình duyệt (Chrome/Edge): cắm cáp USB, mở https://fkit.vn/update?product=fkit, bấm Kết nối, chọn cổng rồi làm theo hướng dẫn.

## [1.1.6] - 2026-10-25

- Flash card lấy thẻ từ máy chủ (bộ từ của tài khoản) qua HTTPS với token thiết bị, lưu bộ nhớ đệm để dùng khi offline; chưa ghép hoặc chưa có dữ liệu thì dùng bộ từ mẫu.

## [1.1.5] - 2026-10-25

- Đồng hồ trên bảng cài đặt nhanh cập nhật mỗi phút thay vì mỗi giây.

## [1.1.4] - 2026-10-25

- Đồng hồ trên bảng cài đặt nhanh cập nhật mỗi phút thay vì mỗi giây.

## [1.1.3] - 2026-10-25

- Thêm đồng hồ (giờ:phút) cạnh biểu tượng pin trên bảng cài đặt nhanh (vuốt từ trên xuống), tự cập nhật; hiện --:-- khi chưa đồng bộ giờ.

## [1.1.2] - 2026-10-25

- Kiểm tra cập nhật OTA không còn chờ đồng bộ giờ (SNTP); chỉ cần có WiFi.

## [1.1.1] - 2026-10-24

- Thêm nút Khởi động lại ngay sau khi cập nhật OTA xong để áp dụng bản mới tức thì (vẫn tự khởi động lại khi rảnh).

## [1.1.0] - 2026-10-24

- Bản kiểm tra cập nhật OTA tự động; không thay đổi tính năng.

## [1.0.9] - 2026-10-24

- Kiểm tra cập nhật OTA hoạt động cả khi thiết bị chưa ghép (vẫn gửi device_id), không còn báo "Lỗi: mạng".

## [1.0.8] - 2026-10-24

- Bản kiểm tra luồng cập nhật OTA (sau bản vá device_id của 1.0.7); không thay đổi tính năng.

## [1.0.7] - 2026-10-24

- Sửa lỗi cập nhật OTA báo "Lỗi: mạng" và mất ghép: tự thêm device_id vào URL tải firmware khi thiết bị đã ghép.

## [1.0.6] - 2026-10-24

- Sửa bước cài gh khi tạo GitHub Release; hạ log tên WiFi (SSID) xuống mức DEBUG.

## [1.0.5] - 2026-10-24

- CI đẩy cả app.bin và full.bin của cùng một bản build lên máy chủ (lưu trên S3) và tự tạo GitHub Release (full.bin, app.bin, SHA256SUMS); chỉ thử lại khi lỗi mạng/429/5xx. README bổ sung phần app.bin và full.bin.

## [1.0.4] - 2026-10-24

- CI tự đẩy file -full.bin của cùng bản build lên server để nạp USB trên web; chỉ thử lại khi lỗi mạng/5xx.

## [1.0.3] - 2026-10-24

- Đổi định dạng tên phiên bản firmware từ FDICT-DM-x.y.z thành FDICT-x.y.z (ví dụ FDICT-1.0.3). Thiết bị chấp nhận ảnh OTA có tiền tố FDICT-; Build ID giữ nguyên dạng fdictdm10 + 4 hex SHA256 ELF.

## [1.0.2] - 2026-10-23

- Loa: sửa mất tiếng sau khi ngủ/thức (ES8311 giữ mute DAC 0x31 qua reset; nay luôn bỏ mute sau init codec); giữ PA tắt qua deep sleep.

## [1.0.1] - 2026-10-23

- Flash card: màn cài đặt trước phiên (kiểu Học tập): Số thẻ 10 / 20 / Tất cả, Thứ tự, Mặt trước (Từ / Nghĩa), Tự động phát; lưu NVS `fc_cfg`; thay màn danh sách thẻ.

## [1.0.0] – 2026-10-23 – Phát hành đầu tiên / Initial release

- Thông tin phiên bản: thêm Bộ nhớ flash, Dữ liệu (/data đã dùng / tổng), Giọng đọc (OK / Thiếu), RAM trống (Nội bộ · PSRAM).
- Thông tin phiên bản: sắp xếp theo nhóm Thiết bị / Phần mềm / Bộ nhớ / Kết nối; nút Kiểm tra cập nhật ở đầu; "Không có bản nâng cấp" khi đã mới nhất.
- Loa: bấm phát âm khi đang phát (hoặc ngay sau âm đúng/sai) được xếp hàng thay vì bị bỏ qua; bỏ log lỗi i2s lúc khởi động.
- Giao diện LVGL 9, bố cục dọc và ngang, font Noto Sans tiếng Việt.
- Học tập: flashcard, trắc nghiệm 4 đáp án lưới 2x2, tự phát âm x1 / x2 / Tắt.
- Dữ liệu giọng Pico (~1,4 MB) chuyển sang phân vùng flash `voice` (mmap, kiểm tra header + CRC32); ứng dụng còn ~1,5 MB. Bảng phân vùng mới: `storage` 6,06 MB + `voice` 1,75 MB — lần đầu phải nạp USB bằng `-full.bin`. Thiếu dữ liệu giọng: TTS tắt, máy vẫn chạy.
- Giảm kích thước firmware ~110 KB: font Montserrat 24/32 rút gọn + nén, logo RGB565A8, Log V2, bỏ IPv6, bỏ Wi-Fi OWE/SAE-PK, bỏ mbedTLS PEM-write/CRL/P-521.
- Học tập: Số câu 10 / 20 / Vô tận (nối nhiều phiên máy chủ, nạp nền trước khi hết câu; offline học vòng lại cache); kết quả từng phiên vào hàng chờ gửi.
- Học tập: Tự động phát Tắt / 1 lần / 2 lần; đáp án không còn tiền tố A–D; nút Bắt đầu ghim đáy màn.
- Học tập mở câu hỏi ngay từ cache trên máy (cache-first), tải bổ sung ở nền; kết nối HTTPS giữ lại (keep-alive) giữa các lần gọi API.
- Học offline: cache bài học từ `/api/study/start` trên LittleFS.
- Kết quả học (online lẫn offline) lưu ngay trên máy (`/data/outbox`, ghi nguyên tử, lưu tiến độ sau mỗi câu), tự gửi `POST /api/study/result` khi có mạng (thử lại có backoff, chống trùng theo `session_id`, giới hạn 50 bản / 512 KB). Màn Home hiển thị số kết quả chờ đồng bộ.
- Phát âm offline bằng SVOX Pico TTS (en-US) nhúng trong firmware.
- WiFi manager, ghép thiết bị bằng QR, OTA (SHA256 + rollback, slot 4 MB).
- Thông báo lỗi HTTP thân thiện (401/410/429/5xx).
- Màn *Thông tin* hiển thị kích thước firmware.
- Màn chào: logo Fdict 1 giây mỗi lần bật màn hình (khởi động và khi thức dậy), chặn thao tác trong lúc hiện.
- Học tập: âm "ting" khi trả lời đúng, âm trầm hai nốt khi sai (tổng hợp trên máy, theo âm lượng); phát trước phần đọc từ.
- Chế độ nhà phát triển (server Lab LAN) mặc định tắt.
- Mã nguồn mở theo Apache-2.0; URL máy chủ là tuỳ chọn Kconfig (placeholder).
