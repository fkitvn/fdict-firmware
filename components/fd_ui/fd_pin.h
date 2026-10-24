#pragma once
/* Ma PIN 4 so: luu NVS "fd" (pin_salt 16 B + pin_hash 32 B = SHA-256 lap co muoi).
 * Khoa: sai 5 lan LIEN TIEP -> khoa 30 s; dem sai + khoa chi trong RAM (khoi dong lai = mo khoa); het khoa / dung -> dem lai tu 0. */
#include <stdbool.h>

bool fd_pin_is_set(void);
bool fd_pin_set(const char digits[4]);   /* luu PIN moi, xoa dem sai */
bool fd_pin_clear(void);
/* true = dung. false = sai hoac dang khoa (xem fd_pin_lock_left_s). */
bool fd_pin_verify(const char digits[4]);
int fd_pin_lock_left_s(void);
int fd_pin_fails_left(void);            /* so lan nhap sai con lai truoc khi khoa (chi doc) */
/* Co 'can PIN khi thuc' (NVS fd/pin_req): dat truoc khi nguoi dung cho ngu, xoa sau khi nhap dung. */
bool fd_pin_req_get(void);
void fd_pin_req_set(bool on);            /* > 0: dang khoa, con N giay */
