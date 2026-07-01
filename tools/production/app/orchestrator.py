"""
orchestrator.py — оркестратор confirm_request для TUI.

Получает события от FirmwareClient, принимает решение кто должен
ответить на confirm_request, и отправляет confirm обратно.

Три режима confirm:

    standalone/interactive (display, mqs, usd):
        → TUI показывает prompt оператору + кнопки OK/FAIL + countdown
        → ждёт действия оператора
        → отправляет confirm

    standalone/buttons:
        → TUI показывает инструкцию оператору
        → НЕ отправляет confirm — таргет сам детектирует нажатие
        → ждёт следующего confirm_request или test_result

    HIL (opto, can):
        → TUI автоматически командует M5 через M5Client
        → отправляет confirm без участия оператора
        → оператор видит только прогресс

Маршрутизация определяется по id confirm_request:
    "opto_*"        → HIL (M5 relay)
    "can_rx_ready"  → HIL (M5 can_send)
    "can_tx_verify" → HIL (M5 can_recv + verify)
    "btn*"          → buttons (нет confirm, ждём test_result)
    всё остальное   → operator (показать prompt)

Помимо confirm_request, протокол v2 определяет "progress" — внутришаговые
информационные события долгих тестов (сейчас только usd: card_detect,
mount, write, read_compare — см. docs/testing/PROTOCOL.md). Эти события
не требуют ответа, только отображаются как текущая фаза прогресса.

Публичный API:
    Orchestrator.run_tests(test_ids)  — запустить тесты, yield OrchestratorEvent
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from enum import Enum, auto
from typing import AsyncGenerator, Callable, Coroutine, Optional

from .firmware_client import FirmwareClient
from .m5_client import M5Client
from .models import ConfirmRequest, TestResult, TestStatus

logger = logging.getLogger(__name__)

# Задержки для HIL
_RELAY_ON_S = 0.15
_RELAY_OFF_S = 0.50

# Карта confirm_id → реле M5 для opto-теста
# Формат: confirm_id → (relay_num, target_state)
_OPTO_RELAY_MAP: dict[str, tuple[int, bool]] = {
    "opto_in1_active": (3, True),
    "opto_in1_inactive": (3, False),
    "opto_in2_active": (4, True),
    "opto_in2_inactive": (4, False),
    "opto_rs_active": (2, True),
    "opto_rs_inactive": (2, False),
}

# CAN параметры
_CAN_RX_ID = 0x100
_CAN_RX_DATA = [0xDE, 0xAD, 0xBE, 0xEF]
_CAN_TX_ID = 0x200
_CAN_TX_DATA = [0xCA, 0xFE, 0xBA, 0xBE]

# Тип события от FirmwareClient._recv_until() сигнализирующий таймаут чтения
_TIMEOUT_EVENT_TYPE = "_timeout"


# ── Типы событий оркестратора ────────────────────────────────────────────


class OrchestratorEventType(Enum):
    TEST_BEGIN = auto()  # тест начался
    TEST_RESULT = auto()  # тест завершился
    TEST_PROGRESS = auto()  # внутришаговый прогресс долгого теста (usd и т.п.)
    CONFIRM_NEEDED = auto()  # нужен ответ оператора (standalone)
    CONFIRM_RESOLVED = auto()  # HIL confirm выполнен автоматически
    BUTTONS_PROMPT = auto()  # показать инструкцию для buttons (без confirm)
    SUMMARY = auto()  # итог всей сессии
    ERROR = auto()  # ошибка протокола, M5, или обрыв по таймауту


@dataclass
class OrchestratorEvent:
    """Событие от оркестратора — передаётся в TUI."""

    type: OrchestratorEventType
    test_id: str = ""
    test_name: str = ""
    result: Optional[TestResult] = None
    confirm: Optional[ConfirmRequest] = None
    summary: Optional[dict] = None
    message: str = ""


# Тип callback для ответа оператора
OperatorConfirmCallback = Callable[[bool], Coroutine]


class Orchestrator:
    """
    Оркестратор confirm_request.

    Пример::

        orch = Orchestrator(firmware_client, m5_client)
        async for event in orch.run_tests(["sdram", "opto", "display"]):
            if event.type == OrchestratorEventType.CONFIRM_NEEDED:
                confirmed = await tui.ask_operator(event.confirm)
                await orch.resolve_operator_confirm(confirmed)
            elif event.type == OrchestratorEventType.TEST_RESULT:
                tui.update_result(event.result)
    """

    def __init__(
        self,
        firmware: FirmwareClient,
        m5: Optional[M5Client] = None,
    ) -> None:
        self._fw = firmware
        self._m5 = m5
        self._operator_queue: asyncio.Queue[bool] = asyncio.Queue(maxsize=1)

    async def resolve_operator_confirm(self, confirmed: bool) -> None:
        """
        TUI вызывает этот метод когда оператор нажал OK или FAIL.
        Разблокирует ожидание внутри run_tests().
        """
        await self._operator_queue.put(confirmed)

    async def run_tests(
        self, test_ids: list[str]
    ) -> AsyncGenerator[OrchestratorEvent, None]:
        """
        Запустить тесты и оркестрировать confirm_request.

        Yields OrchestratorEvent в порядке поступления событий от firmware_test.
        Блокируется на CONFIRM_NEEDED до вызова resolve_operator_confirm().

        Гарантия для вызывающего кода: генератор ВСЕГДА заканчивается событием
        SUMMARY — либо настоящим (от firmware), либо синтетическим при обрыве
        потока (таймаут чтения порта истёк раньше, чем пришёл summary).
        Без этой гарантии TUI не может надёжно понять, что прогон завершился
        (именно это вызывало зависание кнопок при таймауте — см. отчёт).
        """
        current_test_id = ""
        got_summary = False

        async for raw in self._fw.run_selected(test_ids):
            event_type = raw.get("type", "")

            if event_type == "test_begin":
                current_test_id = raw.get("id", "")
                yield OrchestratorEvent(
                    type=OrchestratorEventType.TEST_BEGIN,
                    test_id=current_test_id,
                    test_name=raw.get("name", ""),
                )

            elif event_type == "test_result":
                status = {
                    "pass": TestStatus.PASS,
                    "fail": TestStatus.FAIL,
                    "skip": TestStatus.SKIP,
                }.get(raw.get("status", "fail"), TestStatus.FAIL)

                result = TestResult(
                    id=raw.get("id", ""),
                    status=status,
                    duration_ms=raw.get("ms", 0),
                    detail=raw.get("detail", ""),
                )
                current_test_id = ""
                yield OrchestratorEvent(
                    type=OrchestratorEventType.TEST_RESULT,
                    test_id=result.id,
                    result=result,
                )

            elif event_type == "progress":
                # Внутришаговый прогресс долгого теста (сейчас только usd).
                # Не ошибка — информационное событие, не требует ответа.
                step = raw.get("step", "")
                pstat = raw.get("status", "")
                yield OrchestratorEvent(
                    type=OrchestratorEventType.TEST_PROGRESS,
                    test_id=raw.get("test", current_test_id),
                    message=f"{step}: {pstat}" if step else "progress",
                )

            elif event_type == "confirm_request":
                confirm = ConfirmRequest(
                    id=raw.get("id", ""),
                    prompt=raw.get("prompt", ""),
                    timeout_ms=raw.get("timeout_ms", 30000),
                )
                async for ev in self._handle_confirm(confirm):
                    yield ev

            elif event_type == "summary":
                got_summary = True
                yield OrchestratorEvent(
                    type=OrchestratorEventType.SUMMARY,
                    summary=raw,
                )

            elif event_type == _TIMEOUT_EVENT_TYPE:
                # Чтение порта оборвалось по таймауту раньше, чем пришёл
                # summary. Завершаем текущий тест (если он был в RUNNING)
                # синтетическим FAIL, чтобы таблица результатов не осталась
                # с тестом, навечно подвисшим в "выполняется".
                timeout_s = raw.get("timeout_s", 0)
                logger.error(
                    "Test run aborted: read timeout after %.0fs, current_test=%r",
                    timeout_s,
                    current_test_id,
                )
                if current_test_id:
                    yield OrchestratorEvent(
                        type=OrchestratorEventType.TEST_RESULT,
                        test_id=current_test_id,
                        result=TestResult(
                            id=current_test_id,
                            status=TestStatus.FAIL,
                            duration_ms=0,
                            detail=f"таймаут связи ({timeout_s:.0f}с)",
                        ),
                    )
                yield OrchestratorEvent(
                    type=OrchestratorEventType.ERROR,
                    message=f"Связь с платой прервана (таймаут {timeout_s:.0f}с)",
                )
                # break, не return — нужно дойти до synthetic summary ниже
                break

            else:
                # Неизвестный, но не критичный тип события — логируем для
                # разработчика, не показываем оператору как ошибку (см.
                # отчёт замечание №2: ранее "progress" ошибочно считался
                # неизвестным событием до того как протокол был сверен).
                logger.debug("Unhandled protocol event: %r", raw)

        if not got_summary:
            # Поток событий оборвался (таймаут или обрыв соединения) без
            # настоящего summary от firmware — синтезируем его, чтобы
            # вызывающий код (DiagScreen._run_worker) гарантированно вышел
            # из ожидания и разблокировал кнопки/чекбоксы.
            yield OrchestratorEvent(
                type=OrchestratorEventType.SUMMARY,
                summary={"overall": "fail", "passed": 0, "failed": 0, "aborted": True},
            )

    # ── Маршрутизация confirm ────────────────────────────────────────────

    async def _handle_confirm(
        self, confirm: ConfirmRequest
    ) -> AsyncGenerator[OrchestratorEvent, None]:
        """Определить тип confirm и обработать соответственно."""
        cid = confirm.id

        if cid in _OPTO_RELAY_MAP:
            async for ev in self._handle_hil_opto(confirm):
                yield ev
            return

        if cid == "can_rx_ready":
            async for ev in self._handle_hil_can_rx(confirm):
                yield ev
            return

        if cid == "can_tx_verify":
            async for ev in self._handle_hil_can_tx(confirm):
                yield ev
            return

        if cid.startswith("btn"):
            yield OrchestratorEvent(
                type=OrchestratorEventType.BUTTONS_PROMPT,
                confirm=confirm,
            )
            return

        async for ev in self._handle_operator_confirm(confirm):
            yield ev

    # ── HIL opto ────────────────────────────────────────────────────────

    async def _handle_hil_opto(
        self, confirm: ConfirmRequest
    ) -> AsyncGenerator[OrchestratorEvent, None]:
        relay_num, relay_state = _OPTO_RELAY_MAP[confirm.id]

        if self._m5 is None:
            logger.error("HIL confirm без M5: %s", confirm.id)
            await self._fw.send_confirm(confirm.id, False)
            yield OrchestratorEvent(
                type=OrchestratorEventType.ERROR,
                message=f"M5 не подключён для HIL confirm: {confirm.id}",
            )
            return

        settle_s = _RELAY_ON_S if relay_state else _RELAY_OFF_S
        ok = await self._m5.relay_set(relay_num, relay_state)
        await asyncio.sleep(settle_s)
        await self._fw.send_confirm(confirm.id, ok)

        yield OrchestratorEvent(
            type=OrchestratorEventType.CONFIRM_RESOLVED,
            test_id="opto",
            message=f"RLY{relay_num} {'ON' if relay_state else 'OFF'} → {confirm.id}",
        )

    # ── HIL CAN RX ──────────────────────────────────────────────────────

    async def _handle_hil_can_rx(
        self, confirm: ConfirmRequest
    ) -> AsyncGenerator[OrchestratorEvent, None]:
        if self._m5 is None:
            await self._fw.send_confirm(confirm.id, False)
            yield OrchestratorEvent(
                type=OrchestratorEventType.ERROR,
                message="M5 не подключён для CAN RX confirm",
            )
            return

        ok = await self._m5.can_send(_CAN_RX_ID, _CAN_RX_DATA)
        await self._fw.send_confirm(confirm.id, ok)

        yield OrchestratorEvent(
            type=OrchestratorEventType.CONFIRM_RESOLVED,
            test_id="can",
            message=f"M5 CAN TX id=0x{_CAN_RX_ID:03X} data={_CAN_RX_DATA}",
        )

    # ── HIL CAN TX verify ───────────────────────────────────────────────

    async def _handle_hil_can_tx(
        self, confirm: ConfirmRequest
    ) -> AsyncGenerator[OrchestratorEvent, None]:
        if self._m5 is None:
            await self._fw.send_confirm(confirm.id, False)
            yield OrchestratorEvent(
                type=OrchestratorEventType.ERROR,
                message="M5 не подключён для CAN TX verify",
            )
            return

        frame = await self._m5.can_recv(timeout_ms=500)
        verified = (
            frame is not None
            and frame["id"] == _CAN_TX_ID
            and frame["data"] == _CAN_TX_DATA
        )
        await self._fw.send_confirm(confirm.id, verified)

        msg = (
            f"M5 CAN RX ok: id=0x{_CAN_TX_ID:03X}"
            if verified
            else "M5 CAN RX: фрейм не получен или не совпадает"
        )
        yield OrchestratorEvent(
            type=OrchestratorEventType.CONFIRM_RESOLVED,
            test_id="can",
            message=msg,
        )

    # ── Operator confirm ─────────────────────────────────────────────────

    async def _handle_operator_confirm(
        self, confirm: ConfirmRequest
    ) -> AsyncGenerator[OrchestratorEvent, None]:
        """
        Передать confirm оператору. Блокируется до resolve_operator_confirm().
        TUI должен показать prompt и дать возможность ответить.
        """
        while not self._operator_queue.empty():
            self._operator_queue.get_nowait()

        yield OrchestratorEvent(
            type=OrchestratorEventType.CONFIRM_NEEDED,
            confirm=confirm,
        )

        timeout_s = confirm.timeout_ms / 1000.0
        try:
            confirmed = await asyncio.wait_for(
                self._operator_queue.get(),
                timeout=timeout_s,
            )
        except asyncio.TimeoutError:
            logger.warning("Operator confirm timeout: %s", confirm.id)
            confirmed = False

        await self._fw.send_confirm(confirm.id, confirmed)
