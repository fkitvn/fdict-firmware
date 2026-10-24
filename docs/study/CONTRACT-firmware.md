# Học tập qua API — hợp đồng phía firmware

Firmware: FDICT-1.0.3 (fnk0104b). Màn Học tập không còn dùng bộ từ trên máy; câu hỏi lấy từ server.
Base URL giống OTA/ghép: Lab `http://192.168.1.100:8001`, production `https://your-server.example` (TLS, certificate bundle, không bao giờ tắt kiểm tra chứng chỉ).

## Xác thực
- Mọi request gửi `Authorization: Bearer <device_token>` (NVS `fdict/device_token`, lưu khi ghép) và `Content-Type: application/json`.
- Chưa có token: máy KHÔNG gọi API, hiện "Cần ghép thiết bị để học" + nút Ghép thiết bị.
- 401 (`unauthorized` hoặc `invalid_token`): máy xoá token + tên (giống OTA), về Chưa ghép, hiện thông báo cần ghép.
- Không bao giờ log token, header Authorization hay body.

## 1. Bắt đầu
`POST {base}/api/study/start`
```json
{"device_id":"aabbccddeeff","direction":"en-vi","count":10,"source":"mockup"}
```
- `device_id`: MAC gốc eFuse, 12 hex thường.
- `direction`: `en-vi` | `vi-en`. `count`: 5 | 10 | 20 (mặc định 10). `source`: `mockup` | `deck` (mặc định mockup).

200:
```json
{"session_id":"...","questions":[
  {"id":"q1","prompt":"apple","ipa":"/ˈæp.əl/","choices":[{"key":"A","text":"quả táo"},{"key":"B","text":"..."},{"key":"C","text":"..."},{"key":"D","text":"..."}],"answer":"A"}
]}
```
- en-vi: prompt tiếng Anh, lựa chọn tiếng Việt. vi-en: prompt tiếng Việt, lựa chọn tiếng Anh. `ipa` luôn là phiên âm từ tiếng Anh.
- Máy nhận tối đa 20 câu. Giới hạn độ dài (byte UTF-8, cắt đúng ranh giới ký tự): id/session_id 47, prompt/choice 127, ipa 63. session_id dài hơn 47 byte bị coi là sai định dạng.
- Mỗi câu phải có `id` (chuỗi hoặc số), `prompt`, đủ 4 choices với key A, B, C, D không trùng, và `answer` là một trong A–D. Sai một câu thì cả bộ bị coi là lỗi định dạng (máy báo "Dữ liệu câu hỏi không hợp lệ"). Response tối đa 24 KB.

## 2. Gửi kết quả (một lần)
`POST {base}/api/study/result`
```json
{"device_id":"aabbccddeeff","session_id":"...","answers":[{"id":"q1","choice":"A"},{"id":"q2","choice":""}]}
```
- Có đủ mọi câu theo thứ tự nhận được; câu bỏ qua gửi `choice` rỗng.
- 200 `{"ok":true,"correct":4,"total":5,"score":80}` → màn kết quả hiện correct/total và score%.
- 409 `used` → coi như đã gửi, hiện kết quả tính tại máy, không gửi lại.
- Lỗi mạng / 5xx → nút "Thử lại" gửi lại cùng session_id (an toàn nhờ 409).
- Thoát giữa bài ("Thoát bài làm?") thì không gửi kết quả.

## Giao diện
Setup (Chiều Anh→Việt/Việt→Anh, Số câu 5/10/20, Nguồn Mockup/Bộ thẻ) → Bắt đầu → đang tải → từng câu: "Câu i/n", điểm, nút "Bỏ qua", 4 ô đáp án 2×2 (A, B, C, D = trên-trái, trên-phải, dưới-trái, dưới-phải; màu ô theo phím vật lý, bấm phím màu cũng chọn được) → đúng/sai đánh dấu theo `answer`, tự sang câu sau (0,9 s đúng / 1,5 s sai) → gửi kết quả → Kết quả (Về Home / Học tiếp).
IPA: en-vi hiện dưới từ; vi-en hiện sau khi trả lời. Font Noto 14/18 có IPA Extensions (U+0250–02AF), ˈ ˌ ː.
Chưa Wi-Fi: "Chưa kết nối Wi-Fi" + nút Mở Wi-Fi.

Mã nguồn: `components/fd_ota/fd_study.c` (task `fd_study`, cJSON), `fd_ota_api_post()` trong `fd_ota.c`, UI `components/fd_ui/scr_quiz.c`.
