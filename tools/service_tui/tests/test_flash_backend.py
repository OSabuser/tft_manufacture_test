"""
test_flash_backend.py — юнит-тесты app/flash_backend.py.
uv run pytest tests/test_flash_backend.py -v

Два уровня:
  1. SDP/McuBoot/detect — моки (unittest.mock через monkeypatch). Железо не
     нужно, спсdk-транспорт не трогаем — проверяем только нашу логику
     (последовательность вызовов, обработку ошибок, форму FlashProgress).
  2. build_custom_hab() — НЕ мокается. Использует настоящий HabImage на
     детерминированном dummy-бинаре, как spike_hab.py в Фазе 0 (там же
     доказана byte-exact идентичность nxpimage). Мокать spsdk здесь было бы
     ложной уверенностью — сама суть проверки в том, что реальный HabImage
     не падает и не расходится по конфигу.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import List
from unittest.mock import MagicMock, Mock

import pytest

from app import flash_backend as fb
from app.models import FlashProgress


# ─── Общие фикстуры ──────────────────────────────────────────────────────


@pytest.fixture
def events() -> List[FlashProgress]:
    return []


def _collector(events: List[FlashProgress]):
    return events.append


def _phases(events: List[FlashProgress]) -> list[str]:
    return [e.phase for e in events]


# ─── detect_sdp / detect_cdc ────────────────────────────────────────────


def test_detect_sdp_found(monkeypatch):
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", Mock(return_value=[Mock()]))
    assert fb.detect_sdp() is True


def test_detect_sdp_not_found(monkeypatch):
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", Mock(return_value=[]))
    assert fb.detect_sdp() is False


def test_detect_cdc_found(monkeypatch):
    monkeypatch.setattr(
        fb, "resolve_serial_port", Mock(return_value="/dev/cu.usbmodem1")
    )
    assert fb.detect_cdc() is True


def test_detect_cdc_not_found(monkeypatch):
    monkeypatch.setattr(fb, "resolve_serial_port", Mock(return_value=None))
    assert fb.detect_cdc() is False


def test_detect_flashloader_found(monkeypatch):
    monkeypatch.setattr(fb.MbootUSBInterface, "scan", Mock(return_value=[Mock()]))
    assert fb.detect_flashloader() is True


def test_detect_flashloader_not_found(monkeypatch):
    monkeypatch.setattr(fb.MbootUSBInterface, "scan", Mock(return_value=[]))
    assert fb.detect_flashloader() is False


# ─── wait_for_flashloader ────────────────────────────────────────────────


def test_wait_for_flashloader_immediate(monkeypatch):
    iface = Mock()
    monkeypatch.setattr(fb.MbootUSBInterface, "scan", Mock(return_value=[iface]))
    assert fb.wait_for_flashloader(timeout_s=1.0) is iface


def test_wait_for_flashloader_timeout(monkeypatch):
    monkeypatch.setattr(fb.MbootUSBInterface, "scan", Mock(return_value=[]))
    monkeypatch.setattr(fb.time, "sleep", Mock())  # не ждать реально
    with pytest.raises(fb.FlashLoaderTimeoutError):
        fb.wait_for_flashloader(timeout_s=0.001)


# ─── load_flashloader ────────────────────────────────────────────────────


def test_load_flashloader_already_running(monkeypatch):
    iface = Mock()
    scan_mboot = Mock(return_value=[iface])
    scan_sdp = Mock()  # не должен вызываться
    monkeypatch.setattr(fb.MbootUSBInterface, "scan", scan_mboot)
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", scan_sdp)

    result = fb.load_flashloader()

    assert result is iface
    scan_sdp.assert_not_called()


def test_load_flashloader_no_sdp_device(monkeypatch):
    monkeypatch.setattr(fb.MbootUSBInterface, "scan", Mock(return_value=[]))
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", Mock(return_value=[]))

    with pytest.raises(fb.DeviceNotFoundError):
        fb.load_flashloader()


def test_load_flashloader_missing_bin(monkeypatch):
    monkeypatch.setattr(fb.MbootUSBInterface, "scan", Mock(return_value=[]))
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", Mock(return_value=[Mock()]))
    monkeypatch.setattr(
        fb,
        "flashloader_bin_path",
        Mock(return_value=Mock(exists=Mock(return_value=False))),
    )

    with pytest.raises(fb.FlashBackendError, match="Не найден"):
        fb.load_flashloader()


def test_load_flashloader_happy_path(monkeypatch, events):
    sdp_iface = Mock()
    flashloader_iface = Mock()

    monkeypatch.setattr(fb.MbootUSBInterface, "scan", Mock(return_value=[]))
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", Mock(return_value=[sdp_iface]))

    fake_bin = Mock(
        exists=Mock(return_value=True), read_bytes=Mock(return_value=b"\x00" * 16)
    )
    monkeypatch.setattr(fb, "flashloader_bin_path", Mock(return_value=fake_bin))

    mock_sdp_ctx = MagicMock()
    mock_sdp_ctx.__enter__.return_value = mock_sdp_ctx
    monkeypatch.setattr(fb, "SDP", Mock(return_value=mock_sdp_ctx))

    monkeypatch.setattr(
        fb, "wait_for_flashloader", Mock(return_value=flashloader_iface)
    )

    result = fb.load_flashloader(progress_cb=_collector(events))

    assert result is flashloader_iface
    mock_sdp_ctx.write_file.assert_called_once_with(
        fb.FLASHLOADER_LOAD_ADDR, b"\x00" * 16
    )
    mock_sdp_ctx.jump_and_run.assert_called_once_with(fb.FLASHLOADER_LOAD_ADDR)
    assert _phases(events) == ["load_flashloader", "load_flashloader"]
    assert events[-1].percent == 100


# ─── configure_flexspi / write_fcb_* ─────────────────────────────────────


def test_configure_flexspi_ok():
    mboot = Mock(configure_memory=Mock(return_value=True))
    fb.configure_flexspi(mboot)
    # Р15: option0 (0xC0000207 — включая QE-бит) + явное обнуление option1,
    # зеркало последовательности NXP MCUBootUtility.
    assert mboot.fill_memory.call_args_list == [
        ((fb.FLEXSPI_OPTION_ADDR, 4, fb.FLEXSPI_OPTION_VALUE),),
        ((fb.FLEXSPI_OPTION_ADDR + 4, 4, 0),),
    ]
    mboot.configure_memory.assert_called_once_with(
        fb.FLEXSPI_OPTION_ADDR, fb.FLEXSPI_MEMORY_ID
    )


def test_flexspi_option_value_sets_winbond_qe():
    """Р15-регрессия: quad_mode_setting (биты [11:8]) обязан быть 2 —
    «установить QE-бит в Status Register 2 bit 1» (Winbond W25Q). Со значением
    0 чипы из партий с заводским QE=0 не работают вообще (20106 на любой
    операции) — см. модульный docstring, Р15."""
    assert (fb.FLEXSPI_OPTION_VALUE >> 8) & 0xF == 2
    assert fb.FLEXSPI_OPTION_VALUE == 0xC0000207


def test_configure_flexspi_fail(monkeypatch):
    # Плата на месте → честная ошибка операции, НЕ обрыв (вариант B, Фаза 4a).
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=True))
    monkeypatch.setattr(fb.time, "sleep", Mock())  # не ждать реально между retry (Р14)
    mboot = Mock(configure_memory=Mock(return_value=False))
    with pytest.raises(fb.FlashBackendError) as ei:
        fb.configure_flexspi(mboot)
    assert not isinstance(ei.value, fb.ConnectionLostError)
    assert ei.value.connection_lost is False


def test_write_fcb_auto_uses_magic_word():
    mboot = Mock(configure_memory=Mock(return_value=True))
    fb.write_fcb_auto(mboot)
    mboot.fill_memory.assert_called_once_with(
        fb.FLEXSPI_OPTION_ADDR, 4, fb.FLEXSPI_FCB_VALUE
    )


def test_write_fcb_explicit_missing_file(tmp_path):
    mboot = Mock()
    with pytest.raises(fb.FlashBackendError, match="не найден"):
        fb.write_fcb_explicit(mboot, tmp_path / "nope.bin")


def test_write_fcb_explicit_ok(tmp_path):
    fcb = tmp_path / "w25q128_fdcb.bin"
    fcb.write_bytes(b"\xab" * 512)
    mboot = Mock(write_memory=Mock(return_value=True))

    fb.write_fcb_explicit(mboot, fcb)

    mboot.write_memory.assert_called_once_with(fb.FLASH_BASE, b"\xab" * 512, mem_id=0)


def test_write_fcb_explicit_write_fails(monkeypatch, tmp_path):
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=True))
    monkeypatch.setattr(fb.time, "sleep", Mock())  # не ждать реально между retry (Р14)
    fcb = tmp_path / "w25q128_fdcb.bin"
    fcb.write_bytes(b"\xab" * 512)
    mboot = Mock(write_memory=Mock(return_value=False))

    with pytest.raises(fb.FlashBackendError) as ei:
        fb.write_fcb_explicit(mboot, fcb)
    assert ei.value.connection_lost is False


# ─── flash() ──────────────────────────────────────────────────────────────


def _mock_mcuboot_ctx(monkeypatch, **method_returns):
    """Подменяет fb.McuBoot на context-manager мок с заданными return_value
    у fill_memory/configure_memory/flash_erase_region/write_memory (все True
    по умолчанию, кроме явно переопределённых)."""
    defaults = dict(
        configure_memory=True,
        flash_erase_region=True,
        flash_erase_all=True,
        write_memory=True,
    )
    defaults.update(method_returns)

    ctx = MagicMock()
    ctx.__enter__.return_value = ctx
    for name, ret in defaults.items():
        getattr(ctx, name).return_value = ret

    monkeypatch.setattr(fb, "McuBoot", Mock(return_value=ctx))
    return ctx


def test_flash_missing_file(events, tmp_path):
    with pytest.raises(fb.FlashBackendError, match="Файл не найден"):
        fb.flash(tmp_path / "nope_hab.bin", progress_cb=_collector(events))
    assert events == []


def test_flash_ram_only(monkeypatch, events, tmp_path):
    hab_bin = tmp_path / "fw_hab.bin"
    hab_bin.write_bytes(b"\xd1" + b"\x00" * 63)

    sdp_iface = Mock()
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", Mock(return_value=[sdp_iface]))
    mcuboot_mock = Mock()
    monkeypatch.setattr(fb, "McuBoot", mcuboot_mock)

    mock_sdp_ctx = MagicMock()
    mock_sdp_ctx.__enter__.return_value = mock_sdp_ctx
    monkeypatch.setattr(fb, "SDP", Mock(return_value=mock_sdp_ctx))

    fb.flash(hab_bin, ram_only=True, progress_cb=_collector(events))

    addr = fb.FLASH_BASE + fb.HAB_OFFSET
    mock_sdp_ctx.write_file.assert_called_once_with(addr, hab_bin.read_bytes())
    mock_sdp_ctx.jump_and_run.assert_called_once_with(addr)
    mcuboot_mock.assert_not_called()
    assert _phases(events) == ["load_flashloader", "done"]


def test_flash_ram_only_no_sdp(monkeypatch, events, tmp_path):
    hab_bin = tmp_path / "fw_hab.bin"
    hab_bin.write_bytes(b"\xd1")
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", Mock(return_value=[]))

    with pytest.raises(fb.DeviceNotFoundError):
        fb.flash(hab_bin, ram_only=True, progress_cb=_collector(events))


def test_flash_happy_path_auto_fcb(monkeypatch, events, tmp_path):
    hab_bin = tmp_path / "fw_hab.bin"
    hab_bin.write_bytes(b"\xd1" + b"\x00" * 127)

    flashloader_iface = Mock()
    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=flashloader_iface))
    ctx = _mock_mcuboot_ctx(monkeypatch)

    fb.flash(hab_bin, progress_cb=_collector(events))

    ctx.flash_erase_region.assert_called_once()
    args, kwargs = ctx.flash_erase_region.call_args
    assert args[0] == fb.FLASH_BASE
    assert kwargs["mem_id"] == 0

    ctx.write_memory.assert_called_once()
    wargs, wkwargs = ctx.write_memory.call_args
    assert wargs[0] == fb.FLASH_BASE + fb.HAB_OFFSET
    assert wargs[1] == hab_bin.read_bytes()
    assert wkwargs["mem_id"] == 0
    assert "progress_callback" in wkwargs

    ctx.reset.assert_called_once_with(reopen=False)
    assert _phases(events) == ["configure", "erase", "fcb", "write", "reset", "done"]
    assert events[-1].percent == 100


def test_flash_happy_path_explicit_fcb(monkeypatch, events, tmp_path):
    hab_bin = tmp_path / "fw_hab.bin"
    hab_bin.write_bytes(b"\xd1" + b"\x00" * 15)
    fcb_bin = tmp_path / "w25q512_fdcb.bin"
    fcb_bin.write_bytes(b"\xab" * 512)

    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=Mock()))
    ctx = _mock_mcuboot_ctx(monkeypatch)

    fb.flash(hab_bin, fcb_path=fcb_bin, progress_cb=_collector(events))

    # explicit FCB → write_memory вызывается ДВАЖДЫ: FCB-блоб + сам HAB-образ
    assert ctx.write_memory.call_count == 2
    first_call_args = ctx.write_memory.call_args_list[0].args
    assert first_call_args[0] == fb.FLASH_BASE
    assert first_call_args[1] == b"\xab" * 512


def test_flash_write_memory_fails(monkeypatch, events, tmp_path):
    hab_bin = tmp_path / "fw_hab.bin"
    hab_bin.write_bytes(b"\xd1")

    monkeypatch.setattr(
        fb, "detect_flashloader", Mock(return_value=True)
    )  # плата на месте → честная ошибка
    monkeypatch.setattr(fb.time, "sleep", Mock())  # не ждать реально между retry (Р14)
    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=Mock()))
    _mock_mcuboot_ctx(monkeypatch, write_memory=False)

    with pytest.raises(fb.FlashBackendError, match="write_memory"):
        fb.flash(hab_bin, progress_cb=_collector(events))

    assert "reset" not in _phases(events)
    assert "done" not in _phases(events)


def test_flash_progress_callback_reports_bytes(monkeypatch, events, tmp_path):
    """write_memory реально дёргает progress_callback(current, total) — см.
    Фазу 0. Проверяем, что наш адаптер конвертирует это в FlashProgress."""
    hab_bin = tmp_path / "fw_hab.bin"
    payload = b"\xd1" + b"\x00" * 999
    hab_bin.write_bytes(payload)

    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=Mock()))
    ctx = _mock_mcuboot_ctx(monkeypatch)

    def _fake_write_memory(address, data, mem_id=0, progress_callback=None):
        progress_callback(len(data) // 2, len(data))
        progress_callback(len(data), len(data))
        return True

    ctx.write_memory.side_effect = _fake_write_memory

    fb.flash(hab_bin, progress_cb=_collector(events))

    write_events = [e for e in events if e.phase == "write" and "/" in e.message]
    assert len(write_events) == 2
    assert write_events[-1].percent == 100


# ─── erase_chip() ─────────────────────────────────────────────────────────


def test_erase_chip_happy_path(monkeypatch, events):
    flashloader_iface = MagicMock()
    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=flashloader_iface))
    ctx = _mock_mcuboot_ctx(monkeypatch)

    fb.erase_chip(progress_cb=_collector(events))

    assert flashloader_iface.device.timeout == fb.MCUBOOT_CMD_TIMEOUT_MS
    ctx.flash_erase_all.assert_called_once_with(mem_id=fb.FLEXSPI_MEMORY_ID)
    ctx.reset.assert_called_once_with(reopen=False)
    assert _phases(events) == ["configure", "erase", "reset", "done"]


def test_erase_chip_fails(monkeypatch, events):
    monkeypatch.setattr(
        fb, "detect_flashloader", Mock(return_value=True)
    )  # плата на месте → честная ошибка
    monkeypatch.setattr(fb.time, "sleep", Mock())  # не ждать реально между retry (Р14)
    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=MagicMock()))
    _mock_mcuboot_ctx(monkeypatch, flash_erase_all=False)

    with pytest.raises(fb.FlashBackendError, match="flash_erase_all"):
        fb.erase_chip(progress_cb=_collector(events))

    assert "done" not in _phases(events)


# ─── build_custom_hab() — НЕ мокается, реальный HabImage ─────────────────


def _dummy_bin(tmp_path: Path, size: int = 512) -> Path:
    seed = hashlib.sha256(b"test-flash-backend-golden").digest()
    data = (seed * (size // len(seed) + 1))[:size]
    p = tmp_path / "dummy_app.bin"
    p.write_bytes(data)
    return p


def test_build_custom_hab_no_dcd(tmp_path, events):
    raw = _dummy_bin(tmp_path)
    out = fb.build_custom_hab(raw, use_dcd=False, progress_cb=_collector(events))

    assert out.exists()
    assert out.stat().st_size > 0
    assert _phases(events) == ["hab_build", "hab_build"]
    assert events[-1].percent == 100


def test_build_custom_hab_with_dcd(tmp_path):
    real_dcd = fb.real_dcd_bin_path()
    if not real_dcd.exists():
        pytest.skip(f"Реальный DCD не найден: {real_dcd}")
    raw = _dummy_bin(tmp_path)
    out = fb.build_custom_hab(raw, use_dcd=True)
    assert out.exists()
    assert out.stat().st_size > 0


def test_build_custom_hab_dcd_requested_but_missing(monkeypatch, tmp_path):
    raw = _dummy_bin(tmp_path)
    monkeypatch.setattr(
        fb, "real_dcd_bin_path", Mock(return_value=tmp_path / "nonexistent_dcd.bin")
    )

    with pytest.raises(fb.HabBuildError, match="DCD"):
        fb.build_custom_hab(raw, use_dcd=True)


def test_build_custom_hab_missing_input(tmp_path):
    with pytest.raises(fb.HabBuildError):
        fb.build_custom_hab(tmp_path / "does_not_exist.bin", use_dcd=False)


def test_build_custom_hab_bytes_match_golden(tmp_path):
    """Дополнительная перекрёстная проверка со spike_hab.py: два независимых
    вызова build_custom_hab с одинаковым входом дают одинаковый результат
    (детерминированность), а не просто 'не упало'."""
    raw = _dummy_bin(tmp_path)
    out1 = fb.build_custom_hab(raw, use_dcd=False)
    out2 = fb.build_custom_hab(raw, use_dcd=False)
    assert out1.read_bytes() == out2.read_bytes()


# ─── firmware_hab_path() — двухрежимный резолв (Р6) ──────────────────────


def test_firmware_hab_path_dev_default(monkeypatch):
    monkeypatch.delenv("BUILD_DIR", raising=False)
    p = fb.firmware_hab_path("firmware_test", "Debug")
    assert p == fb.REPO_ROOT / "build" / "Debug" / "firmware_test_hab.bin"


def test_firmware_hab_path_dev_env_override(monkeypatch, tmp_path):
    monkeypatch.setenv("BUILD_DIR", str(tmp_path))
    p = fb.firmware_hab_path("bootloader", "Release")
    assert p == tmp_path / "Release" / "bootloader_hab.bin"


def test_firmware_hab_path_frozen(monkeypatch, tmp_path):
    exe = tmp_path / "dist" / "service_tui"
    exe.parent.mkdir(parents=True)
    exe.touch()
    monkeypatch.setattr(fb.sys, "frozen", True, raising=False)
    monkeypatch.setattr(fb.sys, "executable", str(exe))
    p = fb.firmware_hab_path("firmware_test", "Debug")
    assert p == exe.parent / "firmware" / "Debug" / "firmware_test_hab.bin"


# ─── _host_dcd_dir() / flashloader_bin_path() / real_dcd_bin_path() ──────
# Двухрежимный резолв (Р6), симметричный firmware_hab_path() — Фаза 5.


def test_host_dcd_dir_dev_default():
    assert fb._host_dcd_dir() == fb.REPO_ROOT / "tools" / "host" / "dcd"


def test_host_dcd_dir_frozen(monkeypatch, tmp_path):
    meipass = tmp_path / "_internal"
    meipass.mkdir()
    monkeypatch.setattr(fb.sys, "frozen", True, raising=False)
    monkeypatch.setattr(fb.sys, "_MEIPASS", str(meipass), raising=False)
    assert fb._host_dcd_dir() == meipass / "data"


def test_flashloader_bin_path_frozen(monkeypatch, tmp_path):
    meipass = tmp_path / "_internal"
    meipass.mkdir()
    monkeypatch.setattr(fb.sys, "frozen", True, raising=False)
    monkeypatch.setattr(fb.sys, "_MEIPASS", str(meipass), raising=False)
    assert fb.flashloader_bin_path() == meipass / "data" / "ivt_flashloader.bin"


def test_real_dcd_bin_path_dev_default():
    assert fb.real_dcd_bin_path() == fb.REPO_ROOT / "tools" / "host" / "dcd" / "dcd.bin"


# ─── Фаза 4a: типизация обрыва USB ───────────────────────────────────────
#
# Три проявления обрыва (RELEASE_ROADMAP.md):
#   1) SDP/McuBoot бросают SPSDKConnectionError            — ловилось и до 4a;
#   2) read-фаза после write бросает SPSDKTimeoutError     — НЕ ловилось (баг);
#   3) команда возвращает False по таймауту (erase/write)  — вариант B (Р10).


def test_connection_lost_tuple_covers_timeout():
    """Инвариант, на котором держится вся 4a: SPSDKTimeoutError НЕ потомок
    SPSDKConnectionError, поэтому обязан быть в кортеже явно. Страховка от
    случайного регресса при апгрейде spsdk."""
    assert fb.SPSDKTimeoutError in fb._CONNECTION_LOST_EXCEPTIONS
    assert not issubclass(fb.SPSDKTimeoutError, fb.SPSDKConnectionError)


def test_flashloader_still_present_swallows_check_error(monkeypatch):
    """§B: ошибка самой проверки detect_flashloader() трактуется как «устройства нет»."""
    monkeypatch.setattr(
        fb, "detect_flashloader", Mock(side_effect=RuntimeError("bus gone"))
    )
    assert fb._flashloader_still_present() is False


def test_fail_command_device_gone_is_connection_lost(monkeypatch):
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=False))
    mboot = Mock(status_string="NoResponse")
    with pytest.raises(fb.ConnectionLostError) as ei:
        fb._fail_command(mboot, "flash_erase_all вернул False")
    assert ei.value.connection_lost is True
    assert "NoResponse" in str(ei.value)


def test_fail_command_device_present_is_plain_error(monkeypatch):
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=True))
    mboot = Mock(status_string="kStatus_FlashCommandFailure")
    with pytest.raises(fb.FlashBackendError) as ei:
        fb._fail_command(mboot, "flash_erase_all вернул False")
    assert not isinstance(ei.value, fb.ConnectionLostError)
    assert ei.value.connection_lost is False
    assert "kStatus_FlashCommandFailure" in str(ei.value)


# ─── Р14: retry маргинального контакта (_run_flash_cmd) ──────────────────


def test_run_flash_cmd_succeeds_first_try(monkeypatch):
    mboot = Mock(status_string="Success")
    cmd = Mock(return_value=True)
    fb._run_flash_cmd(mboot, "test_cmd", cmd)
    cmd.assert_called_once()


def test_run_flash_cmd_retries_then_succeeds(monkeypatch):
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=True))
    monkeypatch.setattr(fb.time, "sleep", Mock())
    mboot = Mock(status_string="NoResponse")
    cmd = Mock(side_effect=[False, False, True])

    fb._run_flash_cmd(mboot, "test_cmd", cmd)

    assert cmd.call_count == 3


def test_run_flash_cmd_gives_up_after_max_attempts_device_present(monkeypatch):
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=True))
    sleep_mock = Mock()
    monkeypatch.setattr(fb.time, "sleep", sleep_mock)
    mboot = Mock(status_string="kStatus_FlashCommandFailure")
    cmd = Mock(return_value=False)

    with pytest.raises(fb.FlashBackendError) as ei:
        fb._run_flash_cmd(mboot, "test_cmd", cmd)

    assert not isinstance(ei.value, fb.ConnectionLostError)
    assert cmd.call_count == fb.CMD_RETRY_ATTEMPTS
    assert sleep_mock.call_count == fb.CMD_RETRY_ATTEMPTS - 1
    assert f"после {fb.CMD_RETRY_ATTEMPTS} попыт" in str(ei.value)


def test_run_flash_cmd_bails_immediately_if_device_gone(monkeypatch):
    """Обрыв связи между попытками — retry бессмыслен, не тратим время."""
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=False))
    sleep_mock = Mock()
    monkeypatch.setattr(fb.time, "sleep", sleep_mock)
    mboot = Mock(status_string="NoResponse")
    cmd = Mock(return_value=False)

    with pytest.raises(fb.ConnectionLostError):
        fb._run_flash_cmd(mboot, "test_cmd", cmd)

    cmd.assert_called_once()
    sleep_mock.assert_not_called()


def test_load_flashloader_timeout_is_connection_lost(monkeypatch):
    """SPSDKTimeoutError из SDP-обмена → ConnectionLostError (обёртка 4a)."""
    monkeypatch.setattr(fb.MbootUSBInterface, "scan", Mock(return_value=[]))
    monkeypatch.setattr(fb.SdpUSBInterface, "scan", Mock(return_value=[Mock()]))
    monkeypatch.setattr(
        fb,
        "flashloader_bin_path",
        Mock(
            return_value=Mock(
                exists=Mock(return_value=True),
                read_bytes=Mock(return_value=b"\x00" * 16),
            )
        ),
    )
    sdp_ctx = MagicMock()
    sdp_ctx.__enter__.return_value = sdp_ctx
    sdp_ctx.write_file.side_effect = fb.SPSDKTimeoutError()
    monkeypatch.setattr(fb, "SDP", Mock(return_value=sdp_ctx))

    with pytest.raises(fb.ConnectionLostError):
        fb.load_flashloader()


def test_flash_write_memory_timeout_is_connection_lost(monkeypatch, events, tmp_path):
    """Гейт 4a #1: SPSDKTimeoutError из write_memory → ConnectionLostError,
    а не safety-net «Непредвиденная ошибка»."""
    hab_bin = tmp_path / "fw_hab.bin"
    hab_bin.write_bytes(b"\xd1" + b"\x00" * 63)

    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=Mock()))
    ctx = _mock_mcuboot_ctx(monkeypatch)
    ctx.write_memory.side_effect = fb.SPSDKTimeoutError()

    with pytest.raises(fb.ConnectionLostError) as ei:
        fb.flash(hab_bin, progress_cb=_collector(events))
    assert ei.value.connection_lost is True
    assert "reset" not in _phases(events)
    assert "done" not in _phases(events)


def test_flash_write_false_device_gone_is_connection_lost(
    monkeypatch, events, tmp_path
):
    """Вариант B в flash(): write_memory=False + плата пропала → ConnectionLostError."""
    hab_bin = tmp_path / "fw_hab.bin"
    hab_bin.write_bytes(b"\xd1")

    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=Mock()))
    _mock_mcuboot_ctx(monkeypatch, write_memory=False)
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=False))

    with pytest.raises(fb.ConnectionLostError):
        fb.flash(hab_bin, progress_cb=_collector(events))


def test_erase_chip_false_device_gone_is_connection_lost(monkeypatch, events):
    """Гейт 4a #2a: flash_erase_all=False + detect_flashloader=False → ConnectionLostError."""
    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=MagicMock()))
    _mock_mcuboot_ctx(monkeypatch, flash_erase_all=False)
    monkeypatch.setattr(fb, "detect_flashloader", Mock(return_value=False))

    with pytest.raises(fb.ConnectionLostError) as ei:
        fb.erase_chip(progress_cb=_collector(events))
    assert ei.value.connection_lost is True
    assert "done" not in _phases(events)


# ─── Р13: единый таймаут ДО первой Flashloader-команды (не только erase) ──


def test_flash_sets_timeout_before_first_command(monkeypatch, events, tmp_path):
    """Полевой баг: flash_erase_region молча падал по spsdk-дефолту 2000мс,
    т.к. flash() никогда не поднимал iface.device.timeout. Таймаут должен
    быть выставлен ДО configure_flexspi(), а не только перед erase."""
    hab_bin = tmp_path / "fw_hab.bin"
    hab_bin.write_bytes(b"\xd1" + b"\x00" * 15)

    flashloader_iface = Mock()
    flashloader_iface.device.timeout = 2000  # spsdk-дефолт, UsbDevice.__init__
    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=flashloader_iface))
    ctx = _mock_mcuboot_ctx(monkeypatch)

    seen_timeouts = []
    ctx.configure_memory.side_effect = (
        lambda *a, **k: seen_timeouts.append(flashloader_iface.device.timeout) or True
    )

    fb.flash(hab_bin, progress_cb=_collector(events))

    assert seen_timeouts, "configure_memory ни разу не вызван — тест не проверяет ничего"
    assert all(t == fb.MCUBOOT_CMD_TIMEOUT_MS for t in seen_timeouts)


def test_erase_chip_sets_timeout_before_configure(monkeypatch, events):
    """Тот же баг на шаге раньше: до фикса timeout поднимался ПОСЛЕ
    configure_flexspi(), значит сам configure всё ещё шёл на 2000мс."""
    flashloader_iface = MagicMock()
    flashloader_iface.device.timeout = 2000
    monkeypatch.setattr(fb, "load_flashloader", Mock(return_value=flashloader_iface))
    ctx = _mock_mcuboot_ctx(monkeypatch)

    seen_timeouts = []
    ctx.configure_memory.side_effect = (
        lambda *a, **k: seen_timeouts.append(flashloader_iface.device.timeout) or True
    )

    fb.erase_chip(progress_cb=_collector(events))

    assert seen_timeouts, "configure_memory ни разу не вызван — тест не проверяет ничего"
    assert all(t == fb.MCUBOOT_CMD_TIMEOUT_MS for t in seen_timeouts)


# ─── Р13, ОПРОВЕРГНУТАЯ гипотеза — commit FCB (0xF000000F) ДО erase ───────
#
# Была здесь как test_flash_commits_fcb_before_erase /
# test_erase_chip_commits_fcb_before_erase, пинила write_fcb_auto(mboot)
# сразу после configure_flexspi(mboot), до erase (по аналогии с
# boot_utility_log.txt — NXP MCUBootUtility на той же плате). Проверено на
# живом железе (2026-07-13) и ОПРОВЕРГНУТО: configure-memory(0xF000000F)
# физически пишет FCB во flash немедленно, а не просто «донастраивает
# контроллер» — на НЕ стёртой области (то есть на любой ранее прошитой
# плате) запись сразу проваливается со status 10203 «Memory Cumulative
# Write». Сломало ранее рабочую плату. Правка отменена — см. docstring
# модуля flash_backend.py, раздел «Р13, ОПРОВЕРГНУТАЯ гипотеза», не
# повторять без подтверждения по официальной документации NXP.
