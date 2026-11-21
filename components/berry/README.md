# Berry cho FDict One

Ngôn ngữ script Berry (MIT, https://github.com/berry-lang/berry), commit `9f6fbc9`, dùng cho trò chơi tải về (gói `type: "berry"`, firmware 4.9.0 trở lên).

- `src/`: mã nguồn gốc, không sửa (bỏ `be_repl.c`).
- `port/berry_conf.h`: sandbox. Không file, os, sys, debug, gc, introspect, solidify, json, time, global; không nạp bytecode, không thư viện động. Số nguyên 32 bit, số thực float. Cấp phát qua `fd_berry_malloc` (PSRAM, trần 512 KB). Heartbeat mỗi 8192 lệnh.
- `port/be_port.c`: `print` ra log, mọi hàm file trả lỗi.
- `generate/`: bảng hằng số sinh sẵn. Khi đổi `src/` hoặc `berry_conf.h` phải sinh lại:

```
python3 <berry>/tools/coc/coc -o components/berry/generate components/berry/src components/berry/port -c components/berry/port/berry_conf.h
```

Lớp nối với máy (API `fd`, task riêng, giới hạn thời gian) nằm ở `components/fd_berry`.
