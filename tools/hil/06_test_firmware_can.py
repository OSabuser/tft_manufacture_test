"""
06_test_firmware_can.py — HIL тест test_can через firmware_test CDC протокол v2.

Проверяет test_can в составе firmware_test:
  Шаг 1 (RX): при confirm_request "can_rx_ready" M5 отправляет CAN фрейм,
               затем оркестратор подтверждает confirmed:true.
               Таргет принимает фрейм и верифицирует id+data.
  Шаг 2 (TX): таргет отправляет фрейм, затем confirm_request "can_tx_verify".
               Оркестратор принимает фрейм на M5, проверяет id+data,
               отправляет confirmed:true/false.

Запуск:
  just host::hil-firmware-can
  uv run --directory tools/hil pytest 06_test_firmware_can.py -v
"""

import time

import pytest
from conftest import FirmwareCdc, M5Agent

# CAN параметры — должны совпадать с test_can.c
CAN_RX_ID   = 0x100
CAN_RX_DATA = [0xDE, 0xAD, 0xBE, 0xEF]
CAN_TX_ID   = 0x200
CAN_TX_DATA = [0xCA, 0xFE, 0xBA, 0xBE]

# Таймаут для M5 can_recv — с запасом относительно CAN_RX_TIMEOUT_MS (500 мс) в прошивке
CAN_RECV_TIMEOUT_MS = 1000

@pytest.mark.usb_vcom
class TestFirmwareCan:
    """Тест CAN-интерфейса через firmware_test CDC (протокол v2)."""

    @pytest.fixture(autouse=True)
    def _setup(self, firmware_cdc: FirmwareCdc, m5: M5Agent) -> None:
        self.cdc = firmware_cdc
        self.m5 = m5

    
    def _on_confirm(self, confirm_id: str) -> bool:
        """
        Оркестратор CAN-теста.

        can_rx_ready:
          M5 отправляет фрейм на шину ДО confirmed:true.
          Таргет вызовет bsp_can_receive() после confirm.

        can_tx_verify:
          Таргет уже отправил фрейм ДО confirm_request.
          M5 принимает фрейм, проверяет id+data, возвращает результат.
        """
  
        if confirm_id == "can_rx_ready":
            self.m5.can_send(CAN_RX_ID, CAN_RX_DATA)
            # Небольшая пауза чтобы фрейм успел уйти на шину
            time.sleep(0.05)
            return True

        if confirm_id == "can_tx_verify":
            try:
                frame = self.m5.can_recv(timeout_ms=CAN_RECV_TIMEOUT_MS)
                import logging
                logging.getLogger(__name__).info(
                    "M5 received frame: id=0x%X data=%s", 
                    frame.get("id", -1), frame.get("data", [])
                )
            except (TimeoutError, RuntimeError) as e:
                import logging
                logging.getLogger(__name__).warning("M5 can_recv failed: %s", e)
                return False

            # Верифицировать id и data
            if frame.get("id") != CAN_TX_ID:
                return False
            if frame.get("data") != CAN_TX_DATA:
                return False
            return True

        return False

    def test_list_tests(self) -> None:
        """Проверить что 'can' есть в реестре таргета."""
        self.cdc.send({"type": "cmd", "cmd": "list_tests"})
        msg = self.cdc.wait_event("test_list", timeout_s=5.0)
        ids = [t["id"] for t in msg.get("tests", [])]
        print(f"\nЗарегистрированные тесты: {ids}")
        assert "can" in ids, f"'can' не найден в реестре: {ids}"

    def test_ping(self) -> None:
        """Базовая проверка CDC-канала."""
        self.cdc.ping()
        

    def test_can_pass(self) -> None:
        """
        Полный прогон test_can: RX + TX через M5.
        Ожидаем status=pass.
        """
        result = self.cdc.run_hil_test(
            test_id="can",
            on_confirm=self._on_confirm,
            timeout_s=10.0,
        )

        assert result.get("status") == "pass", (
            f"test_can вернул {result.get('status')!r}: "
            f"{result.get('detail', '')}"
        )

    def test_can_rx_fail_no_frame(self) -> None:
        """
        Негативный: при can_rx_ready не отправляем фрейм, но подтверждаем.
        Таргет вызовет bsp_can_receive() и получит timeout → status=fail.
        """

        def bad_confirm(confirm_id: str) -> bool:
            # Подтверждаем без отправки фрейма
            if confirm_id == "can_rx_ready":
                return True
            # can_tx_verify не достигается — тест упадёт раньше
            return False

        result = self.cdc.run_hil_test(
            test_id="can",
            on_confirm=bad_confirm,
            timeout_s=30.0,
        )

        assert result.get("status") == "fail", (
            f"Ожидали fail, получили {result.get('status')!r}"
        )
        detail = result.get("detail", "")
        assert "no frame" in detail, (
            f"detail должен содержать 'no frame': {detail!r}"
        )