# FDict One – Hợp đồng API giọng đọc (firmware)


## 1. Endpoint

`GET {base}/api/v1/audio/{word}?fmt=pcm16k`

- `base`: Lab `https://fdict-dev.joinnow.vn` (chỉ chế độ nhà phát triển), chính thức `https://your-server.example` (TLS, cert bundle, luôn kiểm tra chứng chỉ).
- `word`: `[a-z]{1,31}` (firmware chuyển chữ thường; từ khác không gửi).
- Header: `Authorization: Bearer <device_token>` (NVS `fdict/device_token`). Firmware không log token/header.

## 2. Phản hồi

| Mã | Ý nghĩa | Firmware |
|----|---------|----------|
| 200 | Body = PCM thô 16-bit LE, mono, 16 000 Hz, không header WAV, ≤ 65 536 B, số byte chẵn. `Content-Type: audio/L16;rate=16000;channels=1`, nên có `Content-Length`. Nên cắt im lặng đầu/cuối, chuẩn hoá ~-3 dBFS. | Lưu `/data/voice/<word>.pcm` (ghi `.tmp` rồi rename) |
| 404 | Không có âm cho từ | Không hỏi lại từ này trong 6 h (RAM) |
| 401 | Token không hợp lệ | Xoá token, về "Chưa ghép" (như OTA) |
| khác / lỗi mạng / > 64 KB | | Bỏ, thử lại sau ≥ 60 s |

Không theo redirect phức tạp (tối đa 1). Thời gian chờ 15 s.

## 3. Hành vi thiết bị

- Chạm loa (Flash card): có cache → phát file (đọc theo khối 512 mẫu trong task `fd_audio`); không có → đọc ngay bằng Pico TTS (trên máy) và, nếu có Wi-Fi + đã ghép, xếp tải nền (task `fd_voice`, ưu tiên thấp) cho lần sau.
- Cache tối đa 2 MB trong LittleFS `/data/voice`; vượt → xoá file cũ nhất (mtime).
- Không đóng gói âm thanh vào firmware/ảnh LittleFS.
