"""Chay esp_idf_monitor voi ghi log THOI GIAN THUC vao file chi dinh (bien moi truong FD_MONITOR_LOG).

esp_idf_monitor --save-log co san nhung ghi vao ./log.<elf>.<time>.txt va mo file co dem (khong flush tung dong).
Script nay chi thay Logger.start_logging de mo dung duong dan, che do append, KHONG dem (buffering=0);
moi thu khac (man hinh mau, phim tat Ctrl+T, thoat Ctrl+], giai ma panic) giu nguyen.
"""
import os
import runpy
import sys

LOG = os.environ.get('FD_MONITOR_LOG')
if LOG:
    from esp_idf_monitor.base import logger as _lg

    def _start_logging(self):  # type: ignore[no-untyped-def]
        if not self._log_file:
            try:
                self.log_file = open(LOG, 'ab', buffering=0)
                _lg.log.print('')
                _lg.log.note(f'Logging is enabled into file {LOG}')
            except Exception as e:  # noqa
                _lg.log.print('')
                _lg.log.err(f'Log file {LOG} cannot be created: {e}')

    _lg.Logger.start_logging = _start_logging

sys.argv = ['esp_idf_monitor'] + sys.argv[1:]
runpy.run_module('esp_idf_monitor', run_name='__main__', alter_sys=True)
