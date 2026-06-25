"""
06_test_firmware_opto.py — HIL тест test_opto через firmware_test CDC протокол v2.

Проверяет test_opto в составе firmware_test:
  - 6 шагов: IN1 active/inactive, IN2 active/inactive, RS active/inactive
  - Оркестратор: при confirm_request с известным id командует M5 переключить реле,
    затем отправляет confirmed:true

Маппинг confirm_id → действие M5:
  opto_in1_active   → opto_set(1, True)   (RLY3 ON)
  opto_in1_inactive → opto_set(1, False)  (RLY3 OFF)
  opto_in2_active   → opto_set(2, True)   (RLY4 ON)
  opto_in2_inactive → opto_set(2, False)  (RLY4 OFF)
  opto_rs_active    → opto_set(3, True)   (RLY2 ON)
  opto_rs_inactive  → opto_set(3, False)  (RLY2 OFF)

Запуск:
  just host::hil-firmware-opto
  uv run --directory tools/hil pytest 06_test_firmware_opto.py -v
"""

import time

import pytest
from conftest import FirmwareCdc, M5Agent


RELAY_ON_S  = 0.1   # реле замыкается быстро
RELAY_OFF_S = 0.25    # размыкание + дебаунс прошивки с запасом


# Маппинг confirm_id → (opto_ch, state)
_OPTO_ACTIONS: dict[str, tuple[int, bool]] = {
    "opto_in1_active":   (1, True),
    "opto_in1_inactive": (1, False),
    "opto_in2_active":   (2, True),
    "opto_in2_inactive": (2, False),
    "opto_rs_active":    (3, True),
    "opto_rs_inactive":  (3, False),
}

@pytest.mark.usb_vcom
class TestFirmwareOpto:
    """Тест оптовходов через firmware_test CDC (протокол v2)."""

    @pytest.fixture(autouse=True)
    def _setup(self, firmware_cdc: FirmwareCdc, m5: M5Agent) -> None:
        self.cdc = firmware_cdc
        self.m5 = m5
        # Сбросить все реле перед тестом
        self.m5.opto_all_off()
        time.sleep(RELAY_ON_S)

    def _on_confirm(self, confirm_id: str) -> bool:
        action = _OPTO_ACTIONS.get(confirm_id)
        if action is None:
            return False

        ch, state = action
        self.m5.opto_set(ch, state)
        time.sleep(RELAY_ON_S if state else RELAY_OFF_S)
        return True

    def test_ping(self) -> None:
        """Базовая проверка CDC-канала."""
        self.cdc.ping()

    def test_list_tests(self) -> None:
        """Проверить что 'can' есть в реестре таргета."""
        self.cdc.send({"type": "cmd", "cmd": "list_tests"})
        msg = self.cdc.wait_event("test_list", timeout_s=5.0)
        ids = [t["id"] for t in msg.get("tests", [])]
        print(f"\nЗарегистрированные тесты: {ids}")
        assert "can" in ids, f"'can' не найден в реестре: {ids}"

    def test_opto_pass(self) -> None:
        """
        Запустить test_opto через firmware_test.
        Оркестратор автоматически управляет M5 при каждом confirm_request.
        Ожидаем status=pass.
        """
        result = self.cdc.run_hil_test(
            test_id="opto",
            on_confirm=self._on_confirm,
            timeout_s=60.0,
        )

        assert result.get("status") == "pass", (
            f"test_opto вернул {result.get('status')!r}: "
            f"{result.get('detail', '')}"
        )

    def test_opto_in1_fail_on_inactive(self) -> None:
        """
        Негативный тест: при opto_in1_active НЕ включаем реле.
        Ожидаем status=fail с detail содержащим 'in1' и 'mismatch'.
        """

        def bad_confirm(confirm_id: str) -> bool:
            # Не переключаем реле — канал остаётся INACTIVE
            # Но подтверждаем чтобы тест продолжился
            return True

        result = self.cdc.run_hil_test(
            test_id="opto",
            on_confirm=bad_confirm,
            timeout_s=60.0,
        )

        assert result.get("status") == "fail", (
            f"Ожидали fail, получили {result.get('status')!r}"
        )
        detail = result.get("detail", "")
        assert "mismatch" in detail, (
            f"detail должен содержать 'mismatch': {detail!r}"
        )