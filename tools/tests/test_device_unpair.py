"""Device revocation is asynchronous, ordered, queue-safe and offline-safe."""
import unittest
from test_release_ui import text

class DeviceUnpair(unittest.TestCase):
    def test_worker_revokes_before_local_erase(self):
        s = text("components/fd_ota/fd_ota.c")
        worker = s.split("static void pair_unpair(void)")[1].split("/* ---------- task")[0]
        self.assertLess(worker.index('fd_ota_api_post_ex("/api/devices/unpair"'), worker.index('token_save(NULL)'))
        self.assertIn('fd_ota_is_paired() && fd_net_is_connected()', worker)
        self.assertIn('sizeof(response), false)', worker)
        self.assertNotIn('return', worker)
        self.assertIn('name_save(NULL)', worker)
    def test_ui_does_not_do_http_and_queue_overflow_keeps_request(self):
        s = text("components/fd_ota/fd_ota.c")
        caller = s.split('void fd_ota_unpair(void)')[1].split('/* Doi server')[0]
        self.assertIn('s_unpair_pending = true', caller)
        self.assertNotIn('fd_ota_api_post', caller)
        self.assertIn('if (cmd == CMD_UNPAIR) cmd = 0;', s)
        self.assertIn('if (unpair) pair_unpair();', s)
    def test_401_clears_credential(self):
        s = text("components/fd_ota/fd_ota.c")
        handler = s.split('static bool auth_401(')[1].split('\n}', 1)[0]
        self.assertIn('token_save(NULL)', handler)
    def test_unpair_wakeup_never_discards_other_commands(self):
        s = text("components/fd_ota/fd_ota.c")
        worker = s.split('static void ota_task(void *arg)')[1]
        self.assertNotIn('cmd = CMD_UNPAIR', worker)
        self.assertLess(worker.index('if (unpair) pair_unpair();'), worker.index('if (cmd == CMD_PAIR_START)'))