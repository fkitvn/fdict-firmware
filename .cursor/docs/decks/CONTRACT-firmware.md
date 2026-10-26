# Bo tu offline - hop dong API firmware <-> server (v1)

Firmware: `components/fd_ota/fd_deck.c`. Moi request mang `Authorization: Bearer <device_token>`, base URL nhu OTA (`https://your-server.example/api`, TLS cert bundle). 401 o bat ky route nao -> thiet bi ve "Chua ghep" (GIU outbox).

> **Server can lam:** middleware `fdict-web-app/server/middleware/api-post-only.js` dang chan GET voi prefix khong cong khai -> phai cho phep GET `/api/v1/decks` va `/api/v1/decks/*`.

## 1. GET /api/v1/decks  (JSON <= 8 KB)
    {"decks":[{"id":"core500","name":"500 tu co ban","version":3,"words":500,"default":true}]}
- `id`: `[A-Za-z0-9_-]{1,23}`; `version`: so nguyen, tang moi lan doi noi dung.
- Thiet bi chon bo `default:true` (khong co -> bo dau tien). Trung `id`+`version` voi bo da luu -> khong tai lai.

## 2. GET /api/v1/decks/{id}/words.tsv
UTF-8, moi dong `word<TAB>ipa<TAB>nghia_vi`; dong bat dau `#` la chu thich. >= 4 dong, <= 512 KB, <= 4000 dong; `word` <= 31 byte.

## 3. GET /api/v1/decks/{id}/audio.pak  (tuy chon; 404 = bo khong co am)
Little-endian:
    header 16 B : "FDPK" | u32 fmt=1 | u32 count | u32 reserved
    entry  40 B : char word[32] (NUL-pad) | u32 off | u32 len   (sap xep tang theo strcmp(word))
    data        : PCM 16-bit LE mono 16 kHz; off tinh tu dau file
Tong tsv+pak <= quota 4 MB (`FD_DECK_QUOTA`). Tai vao `/data/decks/<id>.new`, kiem tra hop le roi rename; bo cu bi xoa.

### Header tai file (muc 2, 3)
- Server tra `ETag` (doi khi noi dung doi), `Accept-Ranges: bytes`, `Content-Length`.
- Tai tiep: `Range: bytes=N-` + `If-Range: <etag>` -> `206` + `Content-Range`; ETag khac -> `200` toan bo; N >= size -> `416` (thiet bi xoa phan do, tai lai).
- `If-None-Match: <etag>` -> `304` neu khong doi.

## 4. POST /api/v1/study/offline-result
Moi phien offline = `/data/outbox/<session_id>.json` (toi da 50, day -> bo cu nhat), gui khi online (thu lai moi 60 s):
    {"device_id":"...","session_id":"off-<devid>-<rand>","deck_id":"core500","deck_version":3,
     "direction":"en-vi","answers":[{"word":"apple","choice":"B","correct":true}],"correct":7,"total":10}
- `answers` du moi cau (man hoc khong con nut "Bo qua").
- `200` -> xoa file. `409` (session_id da nhan) -> coi la xong, xoa. `401` -> dung, giu outbox. Loi khac / mang -> giu, thu lai.
- Server phai idempotent theo `session_id`.

## 5. Phat am tren thiet bi
Mac dinh: am trong `audio.pak` cua bo da luu -> cache `/data/voice/<word>.pcm` (GET /api/v1/audio/{word}) -> Pico TTS.
Man Hoc tap co tuy chon (NVS `fd/pron_cfg`): Tu dong / Chi Pico / Co san (khong Pico, bip neu thieu), lap 1/2 lan, tai nen bat/tat.

