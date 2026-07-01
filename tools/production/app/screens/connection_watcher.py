"""
connection_watcher.py — мониторинг USB-соединения для экранов FlashScreen
и DiagScreen.

Периодически (каждые _WATCH_INTERVAL_S) проверяет, виден ли таргет ещё
на шине USB. При потере соединения — экран обязан немедленно прекратить
любое взаимодействие (кроме кнопки "Выйти") и вернуться на WaitingScreen.
Решение архитектурно: при разрыве сессия считается недостоверной и
не восстанавливается — экран не пытается определить "вернулась ли та же
самая плата", просто стартует заново с нуля.

Используется как миксин: класс экрана наследует ConnectionWatcherMixin
вторым родителем после Screen, вызывает self._start_connection_watch(check_fn)
в on_mount(), и переопределяет _on_connection_lost() для специфичной
очистки экрана перед уходом на WaitingScreen.
"""

from __future__ import annotations

from typing import Callable

from textual.message import Message
from textual.timer import Timer

_WATCH_INTERVAL_S = 1.5


class ConnectionLost(Message):
    """
    Соединение с платой потеряно во время нахождения на экране.
    Экран должен прекратить взаимодействие и вернуться на WaitingScreen.
    """


class ConnectionWatcherMixin:
    """
    Миксин периодической проверки USB-соединения.

    Использование в экране::

        class FlashScreen(Screen, ConnectionWatcherMixin):
            def on_mount(self) -> None:
                self._start_connection_watch(Flasher.detect_sdp)

            def on_unmount(self) -> None:
                self._stop_connection_watch()
    """

    _connection_watch_timer: Timer | None = None
    _connection_lost: bool = False

    def _start_connection_watch(self, check_fn: Callable[[], bool]) -> None:
        """
        Запустить периодическую проверку. check_fn должна вернуть True
        пока устройство видно на шине (Flasher.detect_sdp / detect_cdc).
        """
        self._connection_lost = False
        self._connection_check_fn = check_fn
        self._connection_watch_timer = self.set_interval(  # type: ignore[attr-defined]
            _WATCH_INTERVAL_S, self._check_connection
        )

    def _stop_connection_watch(self) -> None:
        if self._connection_watch_timer is not None:
            self._connection_watch_timer.stop()
            self._connection_watch_timer = None

    def _check_connection(self) -> None:
        if self._connection_lost:
            return
        try:
            still_present = self._connection_check_fn()
        except Exception:
            still_present = False
        if not still_present:
            self._connection_lost = True
            self._stop_connection_watch()
            self.post_message(ConnectionLost())  # type: ignore[attr-defined]
