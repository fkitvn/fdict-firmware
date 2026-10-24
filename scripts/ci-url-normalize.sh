#!/usr/bin/env bash
# Chuan hoa DICT_API_URL (Actions variable hoac secret) -> DICT_BASE = https://host[:port].
# Dung chung cho buoc Mask, scripts/ci-ota-build.sh va buoc Publish. KHONG bao gio in gia tri (chi do dai).
#   - bo CR/LF, khoang trang dau/cuoi; phai bat dau bang https://
#   - chi giu scheme + host[:port]: bo userinfo, duong dan, query, fragment (canh bao neu co cat)
#   - GitHub Actions: add-mask gia tri tho + gia tri chuan hoa (co/khong https://) TRUOC moi lenh in
# Dung: source file nay roi goi fd_norm_url (dat bien DICT_BASE), hoac chay truc tiep:
#   bash scripts/ci-url-normalize.sh   -> ghi DICT_BASE vao $GITHUB_ENV (neu co)

fd_norm_url() {
  local raw="${DICT_API_URL:-}" v rest auth host dropped
  if [[ -n "${GITHUB_ACTIONS:-}" && -n "$raw" ]]; then
    # che tung dong cua gia tri tho (co the co xuong dong)
    while IFS= read -r line; do [[ -n "$line" ]] && echo "::add-mask::$line"; done <<< "$raw"
  fi
  v="${raw//$'\r'/}"; v="${v//$'\n'/}"
  v="${v#"${v%%[![:space:]]*}"}"; v="${v%"${v##*[![:space:]]}"}"
  # bo 1 cap ngoac kep/don bao quanh (vd dan nham "https://host") roi cat khoang trang lan nua
  if [[ ${#v} -ge 2 && ( ( "$v" == \"*\" ) || ( "$v" == \'*\' ) ) ]]; then
    v="${v:1:${#v}-2}"
    v="${v#"${v%%[![:space:]]*}"}"; v="${v%"${v##*[![:space:]]}"}"
  fi
  if [[ -z "$v" ]]; then
    echo "::error::DICT_API_URL rong: chua dat variable/secret hoac job thieu environment: production" >&2
    return 1
  fi
  if [[ "$v" != https://* ]]; then
    echo "::error::DICT_API_URL phai bat dau bang https:// (do dai ${#v})" >&2
    return 1
  fi
  rest="${v#https://}"
  auth="${rest%%[/?#]*}"          # authority = phan truoc / ? #
  host="${auth##*@}"               # bo userinfo
  if [[ -n "${GITHUB_ACTIONS:-}" && -n "$host" ]]; then
    echo "::add-mask::https://$host"
    echo "::add-mask::$host"
  fi
  if [[ -z "$host" || ! "$host" =~ ^[A-Za-z0-9.-]+(:[0-9]{1,5})?$ || "$host" == :* ]]; then
    echo "::error::DICT_API_URL host khong hop le (chi https://host[:port]; do dai host ${#host})" >&2
    return 1
  fi
  dropped=$(( ${#rest} - ${#host} ))
  if (( dropped > 0 )); then
    echo "::warning::DICT_API_URL co kem duong dan, da tu cat chi lay https://host (bo ${dropped} ky tu)"
  fi
  DICT_BASE="https://$host"
  echo "DICT_API_URL: do dai ${#raw} -> ${#DICT_BASE} sau chuan hoa"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  set -euo pipefail
  fd_norm_url
  if [[ -n "${GITHUB_ENV:-}" ]]; then echo "DICT_BASE=$DICT_BASE" >> "$GITHUB_ENV"; fi
fi
