"""
test_can.py — HIL тест bsp_can через M5StampPLC.

Стенд:
  M5StampPLC (SIT1044 трансивер) и таргет (SN65HVD230D) подключены
  к общей двухузловой CAN-шине. Скорость: 125 kbit/s.

  Топология и направления трафика:
    M5 TX → шина → таргет RX   (команды: m5.can_send())
    таргет TX → шина → M5 RX   (команды: uart_cmd CAN_SEND, m5.can_recv())

  Важно: bsp_can инициализирован с disableSelfReception=true.
  Таргет не слышит собственные фреймы. Для проверки «таргет TX → таргет RX»
  нужен M5 как ретранслятор (тест test_target_tx_m5_rx_target_rx).

Цепочка фикстур (scope=module):

  m5 (питание ON)
   └── loaded_hil_can (грузит ELF через pyOCD)
         └── uart_can (открывает VCOM, ждёт READY)
               └── _setup (autouse, function scope) → self.ser / self.m5

Запуск:
  just host::hil-can
  uv run pytest test_can.py -v
  uv run pytest test_can.py -v --no-load --m5-port /dev/ttyACM1
"""

import time

import pytest
from conftest import uart_cmd

# ---------------------------------------------------------------------------
# Временны́е константы
# ---------------------------------------------------------------------------

# При 125 kbit/s один фрейм занимает ~0.1 мс.
# Добавляем запас на задержку USB CDC (M5 ↔ хост) и polling в прошивке.
CAN_SETTLE_S = 0.05  # ждать после отправки перед чтением

# Таймаут CAN_RECV на таргете (мс) — передаётся в команду.
# Должен быть достаточным для round-trip через шину + USB CDC M5.
CAN_RECV_TIMEOUT_MS = 300

# Таймаут CAN_RECV при тесте «никто не шлёт» — чтобы тест не завис.
CAN_RECV_EMPTY_MS = 100


# ---------------------------------------------------------------------------
# Вспомогательные функции
# ---------------------------------------------------------------------------


def can_send_m5(m5, can_id: int, data: list[int], ext: bool = False) -> None:
    """M5 отправляет CAN-фрейм на шину."""
    m5.can_send(can_id, data, ext=ext)


def can_recv_m5(m5, timeout_ms: int = 500) -> dict:
    """
    M5 ждёт CAN-фрейм с шины.
    Возвращает dict с ключами id, ext, data.
    Выбрасывает TimeoutError если фрейм не пришёл.
    """
    return m5.can_recv(timeout_ms=timeout_ms)


def can_send_target(ser, can_id: int, data: list[int], ext: bool = False) -> str:
    """
    Таргет отправляет CAN-фрейм.
    Возвращает ответ прошивки: 'OK', 'ERR_TIMEOUT', 'ERR_BUSY', 'ERR_PARAM'.
    """
    dlc = len(data)
    data_str = " ".join(str(b) for b in data)
    ext_flag = 1 if ext else 0
    cmd = f"CAN_SEND {can_id} {ext_flag} {dlc}"
    if dlc > 0:
        cmd += f" {data_str}"
    return uart_cmd(ser, cmd)


def can_recv_target(ser, timeout_ms: int = CAN_RECV_TIMEOUT_MS) -> dict | None:
    """
    Таргет ждёт CAN-фрейм (polling).
    Возвращает dict {id, ext, dlc, data} или None при TIMEOUT.
    """
    resp = uart_cmd(ser, f"CAN_RECV {timeout_ms}")
    if resp == "TIMEOUT":
        return None
    parts = resp.split()
    if len(parts) < 3:
        raise ValueError(f"Неожиданный ответ CAN_RECV: {resp!r}")
    can_id = int(parts[0])
    ext = bool(int(parts[1]))
    dlc = int(parts[2])
    data = [int(b) for b in parts[3 : 3 + dlc]]
    return {"id": can_id, "ext": ext, "dlc": dlc, "data": data}


def set_filter_target(ser, idx: int, can_id: int, mask: int, ext: bool = False) -> str:
    """Установить RX-фильтр на таргете. Возвращает 'OK' или 'ERR_PARAM'."""
    ext_flag = 1 if ext else 0
    return uart_cmd(ser, f"CAN_FILTER {idx} {can_id} {mask} {ext_flag}")


def reset_events(ser) -> None:
    """Сбросить счётчик RX-событий на таргете."""
    assert uart_cmd(ser, "CAN_RESET_EVENTS") == "OK"


# ---------------------------------------------------------------------------
# Проверка каналов связи
# ---------------------------------------------------------------------------


class TestCanConnectivity:
    """Базовая проверка: таргет и M5 отвечают."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_can, m5):
        self.ser = uart_can
        self.m5 = m5

    def test_target_ping(self):
        """PING → PONG: UART-канал host↔target работает."""
        assert uart_cmd(self.ser, "PING") == "PONG"

    def test_m5_ping(self):
        """M5 agent отвечает на ping."""
        self.m5.ping()

    def test_m5_can_available(self):
        """M5 сообщает что CAN инициализирован (can_ok=true в info)."""
        info = self.m5.info()
        assert info.get("can_ok"), (
            "M5 CAN не инициализирован — проверьте трансивер и agent.py"
        )


# ---------------------------------------------------------------------------
# M5 → таргет
# ---------------------------------------------------------------------------


class TestCanM5ToTarget:
    """M5 отправляет фрейм — таргет принимает."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_can, m5):
        self.ser = uart_can
        self.m5 = m5
        uart_cmd(self.ser, "CAN_ACCEPT_ALL")
        reset_events(self.ser)

    def test_std_frame_received(self):
        """M5 TX STD → таргет принимает с верным ID и данными."""
        payload = [0x11, 0x22, 0x33]
        can_send_m5(self.m5, 0x123, payload)
        frame = can_recv_target(self.ser)
        assert frame is not None, "Таргет не принял фрейм от M5"
        assert frame["id"] == 0x123, f"Неверный ID: {frame['id']:#x}"
        assert frame["ext"] is False, "Ожидали STD-фрейм"
        assert frame["dlc"] == 3, f"Неверный DLC: {frame['dlc']}"
        assert frame["data"] == payload, f"Неверные данные: {frame['data']}"

    def test_ext_frame_received(self):
        """M5 TX EXT → таргет принимает с верным 29-bit ID."""
        payload = [0xAA, 0xBB]
        can_send_m5(self.m5, 0x1ABCDEF, payload, ext=True)
        frame = can_recv_target(self.ser)
        assert frame is not None, "Таргет не принял EXT-фрейм"
        assert frame["id"] == 0x1ABCDEF, f"Неверный EXT ID: {frame['id']:#x}"
        assert frame["ext"] is True, "Ожидали EXT-фрейм"
        assert frame["data"] == payload, f"Неверные данные: {frame['data']}"

    def test_max_dlc_frame(self):
        """M5 TX 8-байтовый фрейм — таргет принимает все 8 байт корректно."""
        payload = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08]
        can_send_m5(self.m5, 0x7FF, payload)
        frame = can_recv_target(self.ser)
        assert frame is not None
        assert frame["dlc"] == 8
        assert frame["data"] == payload

    def test_zero_dlc_frame(self):
        """M5 TX фрейм с DLC=0 — таргет принимает без данных."""
        can_send_m5(self.m5, 0x001, [])
        frame = can_recv_target(self.ser)
        assert frame is not None
        assert frame["dlc"] == 0
        assert frame["data"] == []

    def test_rx_event_counter_increments(self):
        """Каждый принятый фрейм увеличивает счётчик событий."""
        for _ in range(3):
            can_send_m5(self.m5, 0x100, [0xFF])
            can_recv_target(self.ser)  # дождаться приёма

        count = int(uart_cmd(self.ser, "CAN_RX_EVENTS"))
        assert count >= 3, f"Ожидали >= 3 события, получили {count}"


# ---------------------------------------------------------------------------
# Таргет → M5
# ---------------------------------------------------------------------------


class TestCanTargetToM5:
    """Таргет отправляет фрейм — M5 принимает."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_can, m5):
        self.ser = uart_can
        self.m5 = m5

    def test_std_frame_sent(self):
        """Таргет TX STD → M5 принимает с верным ID и данными."""
        payload = [0xDE, 0xAD, 0xBE, 0xEF]
        assert can_send_target(self.ser, 0x456, payload) == "OK"
        frame = can_recv_m5(self.m5)
        assert frame["id"] == 0x456, f"Неверный ID: {frame['id']:#x}"
        assert frame["ext"] is False, "Ожидали STD-фрейм"
        assert frame["data"] == payload, f"Неверные данные: {frame['data']}"

    def test_ext_frame_sent(self):
        """Таргет TX EXT → M5 принимает с верным 29-bit ID."""
        payload = [0x01, 0x02]
        assert can_send_target(self.ser, 0x1FFFFFF, payload, ext=True) == "OK"
        frame = can_recv_m5(self.m5)
        assert frame["id"] == 0x1FFFFFF, f"Неверный EXT ID: {frame['id']:#x}"
        assert frame["ext"] is True, "Ожидали EXT-фрейм"

    def test_max_std_id(self):
        """Максимальный достижимый STD ID через M5 recv — проверяем 0x7FE."""
        # 0x7FF вызывает баг в TWAI-биндинге M5 (known limitation)
        assert can_send_target(self.ser, 0x7FE, [0x00]) == "OK"
        frame = can_recv_m5(self.m5)
        assert frame["id"] == 0x7FE

    def test_max_std_id(self):
        """Максимальный STD ID (0x7FF) передаётся корректно."""
        assert can_send_target(self.ser, 0x7FF, [0x00]) == "OK"
        frame = can_recv_m5(self.m5)
        assert frame["id"] == 0x7FF

    def test_max_ext_id(self):
        """Максимальный EXT ID (0x1FFFFFFF) передаётся корректно."""
        assert can_send_target(self.ser, 0x1FFFFFFF, [0x00], ext=True) == "OK"
        frame = can_recv_m5(self.m5)
        assert frame["id"] == 0x1FFFFFFF

    def test_data_integrity_all_bytes(self):
        """Все 8 байт данных передаются без искажений."""
        payload = [0x00, 0xFF, 0x55, 0xAA, 0x0F, 0xF0, 0x01, 0xFE]
        assert can_send_target(self.ser, 0x300, payload) == "OK"
        frame = can_recv_m5(self.m5)
        assert frame["data"] == payload, (
            f"Данные искажены: ожидали {payload}, получили {frame['data']}"
        )


# ---------------------------------------------------------------------------
# Фильтрация
# ---------------------------------------------------------------------------


class TestCanFiltering:
    """
    Проверка фильтрации по ID: только нужные фреймы проходят,
    остальные блокируются.
    """

    @pytest.fixture(autouse=True)
    def _setup(self, uart_can, m5):
        self.ser = uart_can
        self.m5 = m5
        reset_events(self.ser)

    def test_accept_all_receives_any_id(self):
        """После CAN_ACCEPT_ALL таргет принимает фреймы с любым ID."""
        uart_cmd(self.ser, "CAN_ACCEPT_ALL")
        can_send_m5(self.m5, 0x001, [0x01])
        frame = can_recv_target(self.ser)
        assert frame is not None, "Фрейм не принят после CAN_ACCEPT_ALL"

        can_send_m5(self.m5, 0x7FF, [0x02])
        frame = can_recv_target(self.ser)
        assert frame is not None, "Второй фрейм не принят после CAN_ACCEPT_ALL"

    def test_filter_exact_id_passes(self):
        """set_filter с маской 0x7FF пропускает только точный ID."""
        target_id = 0x123
        # Маска 0x7FF = все 11 бит проверяются → точное совпадение.
        assert set_filter_target(self.ser, 0, target_id, 0x7FF) == "OK"

        can_send_m5(self.m5, target_id, [0xAA])
        frame = can_recv_target(self.ser)
        assert frame is not None, "Фрейм с нужным ID не принят"
        assert frame["id"] == target_id, (
            f"Получен ID {frame['id']:#x}, ожидали {target_id:#x}"
        )

    def test_filter_wrong_id_blocked(self):
        """set_filter с точной маской блокирует другой ID."""
        target_id = 0x123
        wrong_id = 0x456
        assert set_filter_target(self.ser, 0, target_id, 0x7FF) == "OK"

        can_send_m5(self.m5, wrong_id, [0xBB])
        frame = can_recv_target(self.ser, timeout_ms=CAN_RECV_EMPTY_MS)
        assert frame is None, (
            f"Фрейм с ID {wrong_id:#x} прошёл фильтр, хотя не должен был"
        )

    def test_filter_mask_passes_group(self):
        """Маска 0x7F0 пропускает группу ID с одинаковыми старшими битами."""
        base_id = 0x120
        mask = 0x7F0  # проверять биты 11..4, биты 3..0 — игнорировать

        assert set_filter_target(self.ser, 0, base_id, mask) == "OK"

        # 0x123 & ~0x7F0 = различается только в младших 4 битах → должен пройти
        can_send_m5(self.m5, 0x123, [0x01])
        frame = can_recv_target(self.ser)
        assert frame is not None, "0x123 должен пройти маску 0x7F0 для базы 0x120"

        # 0x200 — другая группа → должен блокироваться
        # Сбрасываем входной буфер таргета перед проверкой блокировки
        can_send_m5(self.m5, 0x200, [0x02])
        frame = can_recv_target(self.ser, timeout_ms=CAN_RECV_EMPTY_MS)
        assert frame is None, "0x200 не должен пройти маску для базы 0x120"

    def test_ext_filter_exact_id(self):
        """EXT-фильтр пропускает точный 29-bit ID."""
        target_id = 0x1ABCDEF
        assert set_filter_target(self.ser, 0, target_id, 0x1FFFFFFF, ext=True) == "OK"

        can_send_m5(self.m5, target_id, [0x42], ext=True)
        frame = can_recv_target(self.ser)
        assert frame is not None, "EXT-фрейм с нужным ID не принят"
        assert frame["id"] == target_id
        assert frame["ext"] is True

    def test_accept_all_after_filter(self):
        """CAN_ACCEPT_ALL после set_filter снова принимает все фреймы."""
        # Сначала ставим узкий фильтр
        assert set_filter_target(self.ser, 0, 0x100, 0x7FF) == "OK"

        # Убеждаемся что фрейм с другим ID не проходит
        can_send_m5(self.m5, 0x200, [0x01])
        assert can_recv_target(self.ser, timeout_ms=CAN_RECV_EMPTY_MS) is None

        # Снимаем фильтр
        uart_cmd(self.ser, "CAN_ACCEPT_ALL")

        # Теперь должен пройти любой ID
        can_send_m5(self.m5, 0x200, [0x02])
        frame = can_recv_target(self.ser)
        assert frame is not None, "Фрейм не принят после CAN_ACCEPT_ALL"


# ---------------------------------------------------------------------------
# STD и EXT раздельно
# ---------------------------------------------------------------------------


class TestCanFrameTypes:
    """STD и EXT фреймы не перепутываются — is_extended корректен."""

    @pytest.fixture(autouse=True)
    def _setup(self, uart_can, m5):
        self.ser = uart_can
        self.m5 = m5
        uart_cmd(self.ser, "CAN_ACCEPT_ALL")

    def test_std_frame_is_not_ext(self):
        """Принятый STD-фрейм имеет ext=False."""
        can_send_m5(self.m5, 0x1FF, [0x01])
        frame = can_recv_target(self.ser)
        assert frame is not None
        assert frame["ext"] is False, "STD-фрейм ошибочно помечен как EXT"

    def test_ext_frame_is_not_std(self):
        """Принятый EXT-фрейм имеет ext=True."""
        can_send_m5(self.m5, 0x1FF, [0x01], ext=True)
        frame = can_recv_target(self.ser)
        assert frame is not None
        assert frame["ext"] is True, "EXT-фрейм ошибочно помечен как STD"

    def test_std_id_range_boundary(self):
        """Граничные STD ID: 0x000 и 0x7FF."""
        for can_id in [0x000, 0x7FF]:
            can_send_m5(self.m5, can_id, [0xBB])
            frame = can_recv_target(self.ser)
            assert frame is not None, f"ID {can_id:#x} не принят"
            assert frame["id"] == can_id, f"ID {frame['id']:#x} ≠ {can_id:#x}"
            assert frame["ext"] is False

    def test_ext_id_range_boundary(self):
        """Граничные EXT ID: 0x000 и 0x1FFFFFFF."""
        for can_id in [0x000, 0x1FFFFFFF]:
            can_send_m5(self.m5, can_id, [0xCC], ext=True)
            frame = can_recv_target(self.ser)
            assert frame is not None, f"EXT ID {can_id:#x} не принят"
            assert frame["id"] == can_id, f"EXT ID {frame['id']:#x} ≠ {can_id:#x}"
            assert frame["ext"] is True
