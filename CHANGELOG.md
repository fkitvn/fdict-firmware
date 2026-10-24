# Changelog

Định dạng theo [Keep a Changelog](https://keepachangelog.com/), phiên bản theo [SemVer](https://semver.org/).
Build ID (màn *Thông tin*) = `fdictdm10` + 4 hex đầu SHA256 của ELF.

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
