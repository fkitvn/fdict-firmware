# Fdict – Máy học từ vựng

Firmware cho **Fdict**, máy học từ vựng tiếng Anh cầm tay chạy trên **ESP32-S3** (bo mạch **Freenove FNK0104B**).
Viết bằng C trên ESP-IDF 6.1 và LVGL 9.

_English summary at the end._

## Mục lục

- [Tính năng](#tính-năng)
- [Phần cứng](#phần-cứng)
- [Bảng phân vùng](#bảng-phân-vùng)
- [Yêu cầu build](#yêu-cầu-build)
- [Cấu hình](#cấu-hình)
- [Build và nạp](#build-và-nạp)
- [app.bin và full.bin](#appbin-và-fullbin)
- [Cập nhật OTA](#cập-nhật-ota)
- [Phát hành OTA qua GitHub Actions](#phát-hành-ota-qua-github-actions)
- [Cấu trúc dự án](#cấu-trúc-dự-án)
- [Đóng góp](#đóng-góp)
- [Giấy phép](#giấy-phép)

## Tính năng

- Giao diện LVGL 9, hỗ trợ màn hình dọc 240x320 và ngang 320x240 (xoay trong Cài đặt), font Noto Sans tiếng Việt.
- **Flash card**: màn cài đặt trước phiên (số thẻ, thứ tự, mặt trước, tự động phát âm), thẻ hai mặt chạm để lật.
- **Học tập**: trắc nghiệm 4 đáp án dạng lưới 2x2 (Anh–Việt / Việt–Anh), số câu 10 / 20 / Vô tận, tự phát âm Tắt / 1 lần / 2 lần.
- **Học offline**: bài học tải từ máy chủ được lưu cache trên LittleFS; kết quả lưu vào hàng chờ và tự gửi khi có mạng.
- **Phát âm offline** bằng SVOX Pico TTS (en-US); dữ liệu giọng nằm ở phân vùng flash `voice` riêng. Khi có mạng có thể tải âm thanh từ máy chủ về cache.
- Wi-Fi: quét mạng, lưu nhiều mạng, nhập mật khẩu trên màn hình.
- Ghép thiết bị với tài khoản web bằng mã QR.
- Cập nhật firmware OTA qua HTTPS, kiểm tra SHA256 và tự rollback nếu bản mới lỗi.
- Cài đặt: độ sáng, âm lượng, thời gian tắt màn hình, mã PIN, hướng xoay, thông tin phiên bản; nút Ngủ (deep sleep) trong bảng cài đặt nhanh; hiển thị % pin.
- Một số màn khác (Đoán hình, Điền ký tự, Phát âm, Nhại từ…) hiện vẫn là bản mẫu (mockup).

## Phần cứng

- Freenove **FNK0104B**: ESP32-S3, flash 16 MB, PSRAM 8MB.
- LCD ILI9341 (SPI) + cảm ứng FT6336U (driver FT5x06).
- Codec âm thanh ES8311 (I2S) + mạch khuếch đại loa.
- 4 nút bấm, LED RGB WS2812, đo pin qua ADC.
- Sơ đồ chân: `components/board_fnk0104b/include/board_pins.h`.

## Bảng phân vùng

Flash 16 MB, xem `partitions.csv`:

| Tên      | Loại          | Offset   | Kích thước                   |
| -------- | ------------- | -------- | ---------------------------- |
| nvs      | data/nvs      | 0x9000   | 64 KB                        |
| otadata  | data/ota      | 0x19000  | 8 KB                         |
| phy_init | data/phy      | 0x1B000  | 4 KB                         |
| nvs_keys | data/nvs_keys | 0x1C000  | 4 KB                         |
| ota_0    | app           | 0x20000  | 4 MB                         |
| ota_1    | app           | 0x420000 | 4 MB                         |
| storage  | data/littlefs | 0x820000 | ~6,06 MB                     |
| voice    | data/0x40     | 0xE30000 | 1,75 MB (dữ liệu giọng Pico) |
| coredump | data/coredump | 0xFF0000 | 64 KB                        |

Mỗi slot ứng dụng 4 MB, nên file ứng dụng `.bin` phải nhỏ hơn 4 MB (hiện khoảng 1,5 MB).

## Yêu cầu build

- **ESP-IDF 6.1** (target `esp32s3`).
- Thư viện ngoài (LVGL, esp_lvgl_port, esp_lcd_ili9341, esp_lcd_touch_ft5x06, button, cJSON, littlefs) được **ESP Component Manager** tự tải vào `managed_components/` theo `idf_component.yml` và `dependencies.lock`.
- Python của ESP-IDF (dùng cho `tools/mk_voice.py`, tự chạy khi build).

## Cấu hình

`sdkconfig` được sinh từ `sdkconfig.defaults*` và **không commit** (đã có trong `.gitignore`).

Firmware không chứa địa chỉ máy chủ thật. Đặt URL trong `idf.py menuconfig`:

- `Fdict OTA → Production base URL` (`CONFIG_FD_OTA_BASE_URL`, HTTPS, không kèm `/api`)
- `Fdict OTA → Production: trang ghep tren web` (`CONFIG_FD_PAIR_WEB_URL`)
- `CONFIG_FD_API_BASE_URL` (API thiết bị)

Giá trị mặc định là placeholder `https://your-server.example`. _Chế độ nhà phát triển_ (trong Cài đặt, mặc định tắt) dùng `CONFIG_FD_OTA_LAB_BASE_URL` (HTTP trong mạng LAN, chỉ dùng khi phát triển).

Không commit token, mật khẩu Wi-Fi hay `sdkconfig` chứa thông tin riêng. Hợp đồng API phía thiết bị: `docs/study`, `docs/decks`, `docs/tts`.

## Build và nạp

```sh
# nạp môi trường ESP-IDF 6.1 (export.sh / export.ps1 hoặc profile của EIM)
idf.py set-target esp32s3        # lần đầu
idf.py build
idf.py -p <PORT> flash monitor   # nạp bootloader, bảng phân vùng, ứng dụng và phân vùng voice
```

Bản phát hành: `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.release" build`.

Trên Windows có thể dùng `tools\flash-monitor.ps1 [-Port COM6] [-NoFlash]` để nạp và ghi log monitor vào `logs\`.

Build ID (màn _Thông tin phiên bản_) = `fdictdm10` + 4 ký tự hex đầu của SHA256 file ELF.

### Nạp lần đầu (ảnh đầy đủ)

Máy mới, hoặc máy đang chạy bảng phân vùng cũ (trước 1.0.0, chưa có `voice`), phải nạp **một lần qua USB** bằng ảnh đầy đủ `fdict-firmware-v<ver>-<build>-full.bin` tại offset `0x0`. Ảnh này gồm bootloader, bảng phân vùng, otadata, ứng dụng và `voice`:

```sh
esptool.py --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 fdict-firmware-v<ver>-<build>-full.bin
```

Hoặc dùng web flasher (Chrome/Edge, ví dụ <https://espressif.github.io/esptool-js/>): chọn cổng, thêm file `-full.bin` ở địa chỉ `0x0`, bấm Program.

Khi đổi bảng phân vùng, phân vùng `/data` (LittleFS) bị định dạng lại: mất cache bài học và kết quả chưa gửi. NVS giữ nguyên, không cần ghép lại.

Ảnh đầy đủ được tạo bằng `esptool merge-bin` theo `build/flash_args`:

```sh
esptool.py --chip esp32s3 merge_bin -o full.bin --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x19000 ota_data_initial.bin \
  0x20000 fdict-fw.bin 0xe30000 voice.bin
```

### Dữ liệu giọng đọc

- Cập nhật riêng: `esptool.py --chip esp32s3 -p <PORT> write_flash 0xE30000 fdict-voice-v<ver>.bin`.
- Tạo bằng `python tools/mk_voice.py <ta.bin> <sg.bin> voice.bin` (tự chạy khi build).
- Thiếu hoặc hỏng dữ liệu giọng: máy vẫn chạy, Pico TTS tắt, nút loa báo "Thiếu dữ liệu giọng đọc".

Chỉ cập nhật ứng dụng qua USB (máy đã có bảng phân vùng mới):

```sh
esptool.py --chip esp32s3 -p <PORT> write_flash 0x20000 fdict-firmware-v<ver>-<build>.bin
```

## app.bin và full.bin

| | app.bin | full.bin |
|---|---|---|
| Tên file | `fdict-firmware-vX.Y.Z-<build_id>.bin` | `fdict-firmware-vX.Y.Z-<build_id>-full.bin` |
| Kích thước | ~1,5 MB (phải < 4 MB) | ~16,3 MB (≤ 16 MiB) |
| Nội dung | chỉ chương trình Fdict | ảnh gộp toàn bộ flash |
| Nạp bằng | OTA | USB, từ offset `0x0` (ESP Web Tools / `esptool.py write_flash 0x0 ...`) |

- **app.bin**: chỉ chương trình Fdict, dùng cho OTA; được ghi vào `ota_0` hoặc `ota_1` (mỗi phân vùng 4 MB) nên phải < 4 MB. Không đụng bootloader, `/data` (LittleFS) hay `voice`. **KHÔNG** nạp app.bin ở offset `0x0`.
- **full.bin**: ảnh gộp (merged image) nạp từ `0x0` qua USB, gồm:

| Offset | Thành phần |
|---|---|
| `0x0` | bootloader |
| `0x8000` | bảng phân vùng (partition table) |
| `0x19000` | otadata |
| `0x20000` | ứng dụng (`ota_0`) |
| `0xE30000` | dữ liệu giọng `voice` (Pico) |

**Khi nào dùng cái nào:** máy mới hoặc máy lỗi → nạp full.bin qua USB; máy đang chạy → cập nhật app.bin qua OTA. CI tạo cả hai từ **cùng một bản build** và đẩy lên máy chủ (`/api/admin/releases` cho app.bin, `/api/admin/releases/full` cho full.bin). File được lưu trên object storage (S3/MinIO) của máy chủ.

## Cập nhật OTA

OTA chỉ cập nhật **ứng dụng** (file `fdict-firmware-v<ver>-<build>.bin`, không dùng `-full.bin`). Bảng phân vùng và dữ liệu giọng không đổi qua OTA.

1. Thiết bị đã ghép, có Wi-Fi và đã đồng bộ giờ sẽ định kỳ gọi `GET {base}/api/ota/check` với các tham số `hw`, `channel`, `fw_version`, `build_id`, `device_id`, `battery`, `rssi`.
2. Máy chủ trả JSON. Nếu `update` là `false`, thiết bị coi là bản mới nhất (`check_again_s` là chu kỳ kiểm tra lại, tuỳ chọn). Nếu `update` là `true`, cần các trường `release_id`, `fw_version`, `sha256`, `size`, `url` (HTTPS). Các trường tuỳ chọn: `build_id`, `notes_vi`, `mandatory`, `min_battery`.
3. Thiết bị **không tự so sánh số phiên bản**: máy chủ quyết định có bản mới hay không. Sau khi tải, thiết bị kiểm tra SHA256 của ảnh khớp `sha256`, và phiên bản trong ảnh (bắt đầu bằng `FDICT-`) khớp `fw_version`.
4. Thiết bị ghi vào slot OTA còn lại rồi khởi động lại. Bản mới tự kiểm tra (màn hình, bộ nhớ, Wi-Fi), lỗi thì rollback. Trạng thái được báo về `POST {base}/api/ota/report`.

Phiên bản firmware là `PROJECT_VER` trong `CMakeLists.txt` gốc (ví dụ `FDICT-1.0.3`).

## Phát hành OTA qua GitHub Actions

Workflow `.github/workflows/ota-prod.yml` chạy khi **push lên nhánh `prod`**:

1. Build ứng dụng trong container `espressif/idf:v6.1` bằng `scripts/ci-ota-build.sh`.
2. Upload artifact (file app `.bin` và `build-info.json`).
3. Đăng bản lên máy chủ: `POST {DICT_API_URL}/api/admin/releases`, multipart, header `X-CI-Token`, kênh `stable`, `rollout_percent=100`.
4. Tải `-full.bin` của cùng bản build lên máy chủ: `POST {DICT_API_URL}/api/admin/releases/full` (gắn vào `release_id` vừa đăng), cho trình nạp USB trên web.
5. Tạo **GitHub Release** tag `v<semver>` (tiêu đề `Fdict FDICT-<semver>`) với `...-full.bin`, `...-app.bin` và `SHA256SUMS.txt`, ghi chú lấy từ CHANGELOG. Mỗi bản prod đều xuất hiện ở mục Releases của repo; tag đã có thì bỏ qua, không ghi đè.

Bước OTA chỉ đẩy **bản app** (< 4 MB); `-full.bin` đi qua endpoint riêng ở bước 4.

### Biến và secret cần tạo

Job chạy trong environment **`production`**. Vào Settings → Environments → `production` (hoặc Settings → Secrets and variables → Actions) và tạo:

| Tên | Loại | Giá trị |
|---|---|---|
| `DICT_API_URL` | Variable **hoặc** Secret | URL gốc máy chủ `https://host`. Workflow tự chuẩn hoá: bỏ khoảng trắng, xuống dòng, ngoặc bao quanh; chỉ giữ `https://host[:port]`, nếu có kèm đường dẫn/query thì tự cắt và báo cảnh báo. |
| `CI_RELEASE_TOKEN` | **Secret** (bắt buộc) | token CI của máy chủ (header `X-CI-Token`) |

Workflow đọc `vars.DICT_API_URL`, nếu trống thì đọc `secrets.DICT_API_URL`. Bước *Mask server URL* (`scripts/ci-url-normalize.sh`) chuẩn hoá giá trị thành `DICT_BASE`, các bước sau dùng `DICT_BASE`. Variable không được GitHub tự che, nên workflow che giá trị ngay ở bước đầu (`::add-mask::`) và không bao giờ in ra (chỉ in độ dài). GitHub vẫn có thể hiện giá trị variable trong phần `env` của bước đầu tiên; muốn giấu hoàn toàn thì dùng Secret.

Lỗi thường gặp: `DICT_API_URL rong` nghĩa là chưa đặt, hoặc đặt ở environment khác `production`.

Địa chỉ máy chủ **không nằm trong repo**. Khi build, script sinh overlay tạm `build-ci/sdkconfig.ci` từ `DICT_API_URL` để đặt các option `CONFIG_FD_OTA_BASE_URL`, `CONFIG_FD_PAIR_WEB_URL` (`/pairing-device`) và `CONFIG_FD_API_BASE_URL` (`/api/device/v1`). Các cấu hình còn lại lấy từ `sdkconfig.defaults` và `sdkconfig.defaults.esp32s3`. Token và URL không bao giờ được in ra log.

### Cắt một bản mới

1. Tăng `PROJECT_VER` trong `CMakeLists.txt` gốc, ví dụ `FDICT-1.0.4`. Mỗi bản phải có phiên bản mới.
2. Thêm mục `## [1.0.3] - YYYY-MM-DD` **ở đầu** `CHANGELOG.md`, ghi thay đổi bằng tiếng Việt. Nội dung mục này thành ghi chú bản phát hành (tối đa 500 ký tự).
3. Merge hoặc push lên `prod`.

Script dừng build nếu:

- mục đầu CHANGELOG khác `PROJECT_VER`;
- phiên bản ghi trong file bin (`esp_app_desc`) khác `PROJECT_VER`;
- build có cảnh báo;
- file bin từ 4 MB trở lên.

`build_id` = `fdictdm10` + 4 hex đầu SHA256 file ELF, giống màn _Thông tin_.

Máy chủ trả `201` khi đăng thành công (kèm `release_id`, dùng để gắn `-full.bin` và tạo GitHub Release). Nếu trả `409` (bản `fw_version`/`build_id` đã tồn tại), máy chủ chỉ trả `{"error":"exists"}`, không có `release_id`, nên workflow **báo lỗi** và dừng (không tải `-full.bin`, không tạo GitHub Release). Muốn phát hành lại: tăng `PROJECT_VER` trong `CMakeLists.txt`, thêm mục mới ở đầu `CHANGELOG.md`, rồi push lại lên `prod`. Tóm tắt job ghi `fw_version`, `build_id`, kích thước, `sha256`, `release_id` và ghi chú.

### Phát hành dần (canary)

Trong bước _Publish OTA_:

- đổi `rollout_percent=100` thành `1`, `10` hoặc `50`;
- hoặc thêm `-F "allow_devices=<device_id>,<device_id>"` để chỉ một số máy nhận bản.

## Cấu trúc dự án

```
main/                 app_main, thông tin hệ thống
components/
  board_fnk0104b/     chân, LCD, cảm ứng, nút, LED, I2C
  fd_display/         LVGL port, xoay màn hình, đèn nền
  fd_ui/              các màn hình, điều hướng, ngủ
  fd_audio/           ES8311 + I2S, hàng đợi phát
  fd_tts/             SVOX Pico TTS (bên thứ ba)
  fd_voice/           phát âm: cache, Pico, tải nền
  fd_net/             Wi-Fi, đồng bộ giờ
  fd_ota/             OTA, ghép thiết bị, API, cache bài học
  fd_store/           LittleFS
  fd_power/           đo pin
  fd_fonts/           font sinh bằng lv_font_conv
  fd_events/          sự kiện nội bộ
assets/               font, logo gốc
docs/                 hợp đồng API phía thiết bị
scripts/              build CI cho OTA (ci-ota-build.sh)
.github/workflows/    GitHub Actions (ota-prod.yml)
tools/                script tạo voice.bin, font, nạp và monitor
partitions.csv        bảng phân vùng 16 MB
```

## Đóng góp

- Mở issue hoặc pull request. Mô tả rõ thay đổi và cách kiểm tra trên thiết bị.
- Build phải sạch: 0 lỗi, 0 cảnh báo, ứng dụng nhỏ hơn 4 MB.
- Kiểm tra giao diện ở cả màn dọc và ngang.
- Không đưa URL máy chủ thật, token, mật khẩu hay `sdkconfig` vào commit.
- Ghi thay đổi vào [CHANGELOG.md](CHANGELOG.md).

## Giấy phép

Mã nguồn phát hành theo **Apache License 2.0**, xem [LICENSE](LICENSE) và [NOTICE](NOTICE).
Copyright 2026 fkit.vn / Đinh Thanh Thịnh.

Tên **Fdict**, **fkit.vn** và logo Fdict/fkit.vn (`assets/images/fdict-logo-full.svg` và các ảnh dẫn xuất) là thương hiệu thuộc sở hữu của **fkit.vn**, **không** thuộc phạm vi giấy phép Apache-2.0. Muốn sử dụng cần được fkit.vn cho phép.

Thành phần bên thứ ba (SVOX Pico TTS, LVGL, font Noto Sans/Montserrat, biểu tượng…): xem [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## English

**Fdict** is a handheld English-vocabulary trainer on the ESP32-S3 (Freenove FNK0104B: ILI9341 LCD, FT6336U touch, ES8311 audio codec + speaker). Features: LVGL 9 UI (portrait/landscape), flash cards with a pre-session settings screen, 2x2 multiple-choice quizzes, auto pronunciation, offline study (lessons cached on LittleFS, results queued and sent later), offline pronunciation via SVOX Pico TTS (voice data in a separate `voice` partition), Wi-Fi manager, QR device pairing, and HTTPS OTA updates with SHA256 check and rollback.

- **Build:** ESP-IDF 6.1, `idf.py set-target esp32s3 && idf.py build`; flash with `idf.py -p <PORT> flash monitor`.
- **Configure:** server URLs are Kconfig options (`menuconfig`), placeholders by default; `sdkconfig` is git-ignored.
- **First-time flashing:** write `fdict-firmware-v<ver>-<build>-full.bin` at `0x0` over USB (esptool or a web flasher).
- **app.bin vs full.bin:** app.bin (~1.5 MB, < 4 MB) is the application only, for OTA into `ota_0`/`ota_1`, never at `0x0`; full.bin (~16.3 MB) is the merged image written at `0x0` over USB (bootloader `0x0`, partition table `0x8000`, otadata `0x19000`, app `0x20000`, voice `0xE30000`). CI builds both from the same build and uploads them to the server.
- **OTA:** updates the app image only (`fdict-firmware-v<ver>-<build>.bin`). The device calls `GET {base}/api/ota/check`. The server decides whether an update exists and returns `update`, `release_id`, `fw_version`, `sha256`, `size`, `url`. The device verifies the SHA256 and the version string before switching slots.
- **License:** Apache-2.0 (`LICENSE`, `NOTICE`). The Fdict / fkit.vn name and logo are trademarks of fkit.vn and are not covered by the Apache license. Third-party notices: `THIRD_PARTY_NOTICES.md`.
