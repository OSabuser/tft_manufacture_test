"""
m5/agent.py — MicroPython агент для M5Stack StamPLC.
Размещение: /main.py на устройстве.

Деплой:
    just host::m5-deploy

Ручное тестирование:
    just host::m5-cli

─────────────────────────────────────────────────────
Аппаратура (из схемы StamPLC v1.0):

  I2C экспандер  AW9523B   addr=0x59  SDA=G13  SCL=G15
    P0_0..P0_3 → RLY_DRV1..4  (выходы: реле через ULN2003A)
    P1_0..P1_7 → SYS_IN1..8   (входы: оптопары EL3H4, active-low)

  RST AW9523B → G3_PHY_RST (GPIO 3)

  CAN трансивер  SIT1044  TX=G42  RX=G43  (TWAI ESP32-S3)

  Раскладка реле (конфигурация стенда):
    RLY1 → питание таргета  (VIN)
    RLY2 → RS_RX  таргета   (BSP_OPTO_CH_RS)
    RLY3 → EXT_IN1 таргета  (BSP_OPTO_CH_IN1)
    RLY4 → EXT_IN2 таргета  (BSP_OPTO_CH_IN2)

─────────────────────────────────────────────────────
Протокол: JSON-lines через USB CDC (115200, нет flow control).

  Хост → агент:   {"cmd": "<имя>", ...параметры...}\r\n
  Агент → хост:   {"ok": true, ...данные...}\r\n
                  {"ok": false, "err": "<текст>"}\r\n

Команды:
  ping
  info
  aw_debug                              читать сырые регистры AW9523
  relay_set     ch:int(1-4)  state:bool
  relay_get     ch:int(1-4)
  relay_all_off
  power         state:bool              алиас relay_set ch=1
  opto_set      ch:int(1-3)  state:bool
  opto_all_off
  input_read    ch:int(1-8)             читает SYS_IN на самом стенде
  can_send      id:int  list[int]  ext:bool=false
  can_recv      timeout_ms:int=500
"""

import json
import sys
import time

from machine import I2C, Pin

# ---------------------------------------------------------------------------
# Конфигурация
# ---------------------------------------------------------------------------

_CFG = {
    "i2c_sda":  13,
    "i2c_scl":  15,
    "i2c_freq": 400_000,
    "aw_addr":  0x59,
    "can_tx":   42,
    "can_rx":   43,
    "can_baud": 500_000,
}

_RST_PIN = 3                       # G3_PHY_RST → нога сброса AW9523B

_OPTO_TO_RELAY = {1: 3, 2: 4, 3: 2}  # оптоканал таргета → реле стенда
#   ch1 (BSP_OPTO_CH_IN1 / EXT_IN1) → RLY3
#   ch2 (BSP_OPTO_CH_IN2 / EXT_IN2) → RLY4
#   ch3 (BSP_OPTO_CH_RS  / RS_RX  ) → RLY2
_IN_PIN_LIST = [4, 5, 6, 7, 12, 13, 14, 15]  # нумерация пинов AW9523
# ---------------------------------------------------------------------------
# AW9523B — регистры
# ---------------------------------------------------------------------------

_R_IN0   = 0x00   # читать входы P0
_R_IN1   = 0x01   # читать входы P1
_R_OUT0  = 0x02   # писать выходы P0
_R_OUT1  = 0x03   # писать выходы P1
_R_DIR0  = 0x04   # направление P0: 0=выход, 1=вход
_R_DIR1  = 0x05   # направление P1: 0=выход, 1=вход
_R_GCFG  = 0x11   # global config: бит4=P0 режим (0=push-pull)
_R_MODE0 = 0x12   # режим P0: бит=1 → GPIO, бит=0 → LED
_R_MODE1 = 0x13   # режим P1: бит=1 → GPIO, бит=0 → LED

# ---------------------------------------------------------------------------
# AW9523B — класс
# ---------------------------------------------------------------------------

class AW9523:
    """
    Драйвер AW9523B для StamPLC.

    Распределение портов (по схеме):
      P0[0..3] → RLY_DRV1..4 (выходы, управляем реле)
      P1[0..7] → SYS_IN1..8  (входы, active-low оптопары)
    """

    def __init__(self, i2c: I2C, addr: int = 0x59) -> None:
        self._i2c  = i2c
        self._addr = addr
        self._out0 = 0x00

        # 1. Проверяем chip ID (должен быть 0x23)
        chip_id = self._r(0x10)
        if chip_id != 0x23:
            raise Exception("AW9523 not found, chip_id=0x%02x" % chip_id)

        # 2. Все пины — входы по умолчанию
        self._w(_R_DIR0, 0xFF)
        self._w(_R_DIR1, 0xFF)

        # 3. GPIO-режим (не LED) для всех пинов
        self._w(_R_MODE0, 0xFF)
        self._w(_R_MODE1, 0xFF)

        # 4. PUSH-PULL для Port 0: бит 4 в GCFG = 1
        #    openDrainPort0(false) в C++ → bitOn(GCR, 0x10)
        self._w(_R_GCFG, 0x10)

        # 5. Отключить прерывания
        self._w(0x06, 0xFF)
        self._w(0x07, 0xFF)

        # 6. P0_0..P0_3 — выходы (бит=0), P0_4..P0_7 — входы (бит=1)
        self._w(_R_DIR0, 0xF0)
        # P1 весь — входы
        self._w(_R_DIR1, 0xFF)

        # 7. Все выходы выключены
        self._out0 = 0x00
        self._w(_R_OUT0, self._out0)

    # ── низкоуровневое ────────────────────────────────────────────────────

    def _w(self, reg: int, val: int) -> None:
        self._i2c.writeto_mem(self._addr, reg, bytes([val]))

    def _r(self, reg: int) -> int:
        return self._i2c.readfrom_mem(self._addr, reg, 1)[0]

    # ── реле (P0) ─────────────────────────────────────────────────────────

    def relay_set(self, ch: int, state: bool) -> None:
        """ch: 1-4 → P0_0..P0_3."""
        mask = 1 << (ch - 1)
        self._out0 = (self._out0 | mask) if state else (self._out0 & ~mask)
        self._w(_R_OUT0, self._out0)

    def relay_get(self, ch: int) -> bool:
        mask = 1 << (ch - 1)
        return bool(self._out0 & mask)

    def relay_all_off(self) -> None:
        self._out0 = 0x00
        self._w(_R_OUT0, 0x00)

    # ── входы стенда (P1) ─────────────────────────────────────────────────
    
    def input_read(self, ch: int) -> bool:
        """
        ch: 1-8.
        Нумерация пинов AW9523: 0-7=Port0, 8-15=Port1.
        SYS_IN1..4 на Port0 биты 4-7, SYS_IN5..8 на Port1 биты 4-7.
        Полярность: active HIGH (без инверсии) — как в C++ digitalRead.
        """
        pin = _IN_PIN_LIST[ch - 1]
        if pin < 8:
            val = self._r(_R_IN0)
        else:
            val = self._r(_R_IN1)
            pin -= 8
        return bool(val & (1 << pin))

    # ── диагностика ───────────────────────────────────────────────────────

    def debug_regs(self) -> dict:
        return {
            "in0":  self._r(_R_IN0),   # P0 как вход (для отладки)
            "in1":  self._r(_R_IN1),   # P1 — наши реальные входы
            "out0": self._out0,        # теневой регистр реле
            "dir0": self._r(_R_DIR0),
            "dir1": self._r(_R_DIR1),
        }


# ---------------------------------------------------------------------------
# CAN (TWAI ESP32-S3) (для поддержки CAN, используем кастомный загрузчик)
# см. https://github.com/straga/micropython-esp32-twai
# ---------------------------------------------------------------------------

def _make_can():
    try:
        import CAN
        can = CAN(
            0,
            extframe=False,
            tx=42,                    # STAMPLC_PIN_CAN_TX
            rx=43,                    # STAMPLC_PIN_CAN_RX
            mode=CAN.NORMAL,
            bitrate=_CFG["can_baud"], 
            auto_restart=False,
        )
        return can, True
    except Exception as e:
        print("[CAN] ошибка инициализации:", e)
        return None, False



# ---------------------------------------------------------------------------
# Глобальное состояние
# ---------------------------------------------------------------------------

_i2c    = None
_aw     = None
_can    = None
_can_ok = False


# ---------------------------------------------------------------------------
# Диспетчер команд
# ---------------------------------------------------------------------------

def _dispatch(cmd: dict) -> dict:
    c = cmd.get("cmd", "")

    if c == "ping":
        return {"ok": True, "pong": True}

    if c == "info":
        return {
            "ok":     True,
            "fw":     "agent/1.1",
            "python": sys.version[:40],
            "can_ok": _can_ok,
            "aw_ok":  _aw is not None,
            "relays": [_aw.relay_get(i) for i in range(1, 5)] if _aw else [],
        }

    if c == "relay_set":
        if not _aw:
            return {"ok": False, "err": "AW not available"}
        ch    = int(cmd["ch"])
        state = bool(cmd["state"])
        if not (1 <= ch <= 4):
            return {"ok": False, "err": "ch out of range 1..4"}
        _aw.relay_set(ch, state)
        return {"ok": True, "ch": ch, "state": state}

    if c == "relay_get":
        if not _aw:
            return {"ok": False, "err": "AW not available"}
        ch = int(cmd["ch"])
        if not (1 <= ch <= 4):
            return {"ok": False, "err": "ch out of range 1..4"}
        return {"ok": True, "ch": ch, "state": _aw.relay_get(ch)}

    if c == "relay_all_off":
        if not _aw:
            return {"ok": False, "err": "AW not available"}
        _aw.relay_all_off()
        return {"ok": True}

    if c == "power":
        if not _aw:
            return {"ok": False, "err": "AW not available"}
        state = bool(cmd["state"])
        _aw.relay_set(1, state)
        return {"ok": True, "state": state}

    if c == "opto_set":
        if not _aw:
            return {"ok": False, "err": "AW not available"}
        ch    = int(cmd["ch"])
        state = bool(cmd["state"])
        relay = _OPTO_TO_RELAY.get(ch)
        if relay is None:
            return {
                "ok":  False,
                "err": "opto ch %d unknown, valid: 1,2,3" % ch,
            }
        _aw.relay_set(relay, state)
        return {"ok": True, "opto_ch": ch, "relay": relay, "state": state}

    if c == "opto_all_off":
        if not _aw:
            return {"ok": False, "err": "AW not available"}
        for relay in _OPTO_TO_RELAY.values():
            _aw.relay_set(relay, False)
        return {"ok": True}

    if c == "input_read":
        if not _aw:
            return {"ok": False, "err": "AW not available"}
        ch = int(cmd["ch"])
        if not (1 <= ch <= 8):
            return {"ok": False, "err": "ch out of range 1..8"}
        return {"ok": True, "ch": ch, "state": _aw.input_read(ch)}

    if c == "can_send":
        if not _can_ok:
            return {"ok": False, "err": "CAN not available"}
        ext  = bool(cmd.get("ext", False))
        data = bytes(cmd.get("data", []))
        _can.send(list(cmd.get("data", [])), int(cmd["id"]), extframe=ext)
        return {"ok": True}

    if c == "can_recv":
        if not _can_ok:
            return {"ok": False, "err": "CAN not available"}
        timeout_ms = int(cmd.get("timeout_ms", 500))
        msg = _can.recv(timeout=timeout_ms)
        if msg is None:
            return {"ok": False, "err": "timeout"}
        frame_id, _, ext, data = msg
        return {"ok": True, "id": frame_id, "ext": ext, "data": list(data)}

    if c == "can_loopback":
        if not _can_ok:
            return {"ok": False, "err": "CAN not available"}
        try:
            import CAN as _CAN_MOD

            # Временно переинициализируем в LOOPBACK режиме
            _can.deinit()
            lb = _CAN_MOD(
                0,
                extframe=False,
                tx=_CFG["can_tx"],
                rx=_CFG["can_rx"],
                mode=_CAN_MOD.LOOPBACK,
                bitrate=_CFG["can_baud"],
                auto_restart=False,
            )

            test_id   = int(cmd.get("id", 0x123))
            test_data = list(cmd.get("data", [0x01, 0x02, 0x03, 0x04]))
            timeout   = int(cmd.get("timeout_ms", 500))

            lb.send(test_data, test_id)

            deadline = time.ticks_ms() + timeout
            msg = None
            while time.ticks_diff(deadline, time.ticks_ms()) > 0:
                if lb.any():
                    msg = lb.recv()
                    break
                time.sleep_ms(5)

            lb.deinit()

            if msg is None:
                return {"ok": False, "err": "loopback timeout — фрейм не вернулся"}

            recv_id, _, ext, recv_data = msg
            matched = (recv_id == test_id) and (list(recv_data[:len(test_data)]) == test_data)
            return {
                "ok": True,
                "matched": matched,
                "sent_id":   test_id,
                "recv_id":   recv_id,
                "sent_data": test_data,
                "recv_data": list(recv_data),
            }

        except Exception as e:
            return {"ok": False, "err": "loopback error: %s" % e}

    return {"ok": False, "err": "unknown cmd: %r" % c}


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> None:
    global _i2c, _aw, _can, _can_ok

    # Снять AW9523B из аппаратного сброса (G3_PHY_RST = 1)
    try:
        Pin(_RST_PIN, Pin.OUT, value=1)
        time.sleep_ms(10)
    except Exception:
        pass

    # Инициализация AW9523B
    try:
        _i2c = I2C(
            0,
            sda=Pin(_CFG["i2c_sda"]),
            scl=Pin(_CFG["i2c_scl"]),
            freq=_CFG["i2c_freq"],
        )
        _aw = AW9523(_i2c, _CFG["aw_addr"])
    except Exception as exc:
        sys.stdout.write(
            json.dumps({"ok": False, "err": "AW init: %s" % exc}) + "\r\n"
        )

    # Инициализация CAN
    _can, _can_ok = _make_can()

    # Безопасный старт — все реле выключены
    if _aw:
        _aw.relay_all_off()

    sys.stdout.write("READY\r\n")

    # Основной цикл
    while True:
        try:
            line = sys.stdin.readline()
        except Exception:
            continue

        if not line:
            continue
        line = line.strip()
        if not line:
            continue

        try:
            resp = _dispatch(json.loads(line))
        except ValueError as exc:
            resp = {"ok": False, "err": "json: %s" % exc}
        except Exception as exc:
            resp = {"ok": False, "err": str(exc)}

        sys.stdout.write(json.dumps(resp) + "\r\n")


main()