#!/usr/bin/env python3
"""
spike_hab.py — Фаза 0 (де-риск): HAB через HabImage (spsdk Python API)
вместо subprocess `nxpimage hab export`.

Закрывает ⚠В1: подтверждает, что HabImage.load_from_config(...).export()
даёт побайтно идентичный `nxpimage hab export` результат с тем же
конфигом, для DCD on/off.

Реальный вызов подсмотрен в исходнике самого nxpimage —
spsdk/apps/nxpimage_apps/nxpimage_hab.py::hab_export() делает ровно
следующее (это не внутренний вызов apps-модуля, а повтор того же кода
на публичных классах Config/HabImage):

    cfg = Config.create_from_file(yaml_path)
    schemas = HabImage.get_validation_schemas_from_cfg(cfg)
    cfg.check(schemas, check_unknown_props=True)
    hab = HabImage.load_from_config(cfg)
    hab.post_export(cfg.config_dir)
    data = hab.export()

Временный YAML пишется в системный tmp (Р6) с АБСОЛЮТНЫМИ путями
inputImageFile/DCDFilePath — в отличие от текущего flasher.py, который
полагается на cwd=tools/host/hab/ + относительный "../dcd/dcd.bin".
Config.get_input_file_name() резолвит путь через find_file(search_paths=
[cfg_dir]), а абсолютный путь find_file отдаёт как есть независимо от
search_paths (проверено чтением spsdk/utils/config.py) — значит схема
Р6 (temp где угодно, не обязательно рядом с hab/) технически безопасна.

Содержимое "прошивки" для golden-теста не имеет значения — сравнивается
результат УПАКОВКИ (IVT/BDT/[DCD]/APP), а не семантика кода, поэтому
используется детерминированный dummy-бинарь. DCD, наоборот, должен быть
настоящей HAB DCD-командной последовательностью (SegDCD.parse иначе
падает) — поэтому DCD-кейс использует штатный tools/host/dcd/dcd.bin;
если его нет рядом — тест скипается, а не подделывается фиктивным DCD.

Запуск:
    uv run pytest spike_hab.py -v -s     # golden-тест (без железа)
    uv run python spike_hab.py           # то же + подробный вывод в консоль
    uv run python spike_hab.py --dcd-bin /path/to/other_dcd.bin
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional

import pytest
from spsdk.image.hab.hab_image import HabImage
from spsdk.utils.config import Config

# tools/service_tui/spike/spike_hab.py → корень репозитория
REPO_ROOT = Path(__file__).resolve().parents[3]
REAL_DCD_BIN = REPO_ROOT / "tools" / "host" / "dcd" / "dcd.bin"

_HAB_OPTIONS = [
    "options:",
    "  flags: 0x00",
    "  startAddress: 0x60000000",
    "  ivtOffset: 0x1000",
    "  initialLoadSize: 0x2000",
    "  family: mimxrt1050",
]


def _make_dummy_app(size: int = 512) -> bytes:
    """Детерминированные псевдослучайные байты фиксированного размера.

    Не настоящая прошивка — см. docstring модуля: для golden-теста важна
    побайтная идентичность УПАКОВКИ, а не валидность кода приложения.
    """
    seed = hashlib.sha256(b"tft-monolith-phase0-golden-hab").digest()
    return (seed * (size // len(seed) + 1))[:size]


def _write_config(input_bin: Path, dcd_bin: Optional[Path], work_dir: Path) -> Path:
    """Собрать YAML-конфиг nxpimage hab (абсолютные пути, см. docstring модуля)."""
    lines = list(_HAB_OPTIONS)
    if dcd_bin is not None:
        lines.append(f"  DCDFilePath: {dcd_bin.resolve().as_posix()}")
    lines.append(f'inputImageFile: "{input_bin.resolve().as_posix()}"')
    lines.append("sections: []")

    suffix = "dcd" if dcd_bin is not None else "nodcd"
    yaml_path = work_dir / f"hab_golden_{suffix}.yaml"
    yaml_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return yaml_path


def build_via_api(yaml_path: Path) -> bytes:
    """Собрать HAB-образ через spsdk Python API — то же самое, что делает
    `nxpimage hab export` изнутри (см. docstring модуля)."""
    cfg = Config.create_from_file(str(yaml_path))
    schemas = HabImage.get_validation_schemas_from_cfg(cfg)
    cfg.check(schemas, check_unknown_props=True)
    hab = HabImage.load_from_config(cfg)
    hab.post_export(cfg.config_dir)
    return hab.export()


def _require_nxpimage() -> str:
    path = shutil.which("nxpimage")
    if path is None:
        raise RuntimeError(
            "nxpimage не найден в PATH — активирован ли venv tools/service_tui "
            "(spsdk кладёт nxpimage как console_script)?"
        )
    return path


def build_via_cli(yaml_path: Path, out_bin: Path) -> bytes:
    """Собрать тот же образ через `nxpimage hab export` (эталон сравнения)."""
    nxpimage = _require_nxpimage()
    subprocess.run(
        [
            nxpimage,
            "hab",
            "export",
            "--force",
            "-c",
            str(yaml_path),
            "-o",
            str(out_bin),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return out_bin.read_bytes()


def _run_golden(work_dir: Path, dcd_bin: Optional[Path]) -> tuple[bytes, bytes]:
    input_bin = work_dir / "dummy_app.bin"
    input_bin.write_bytes(_make_dummy_app())
    yaml_path = _write_config(input_bin, dcd_bin, work_dir)
    api_bytes = build_via_api(yaml_path)
    cli_bytes = build_via_cli(yaml_path, work_dir / "out_cli.bin")
    return api_bytes, cli_bytes


# ── pytest: остаётся навсегда как регрессия на апгрейды spsdk (Гейт 0) ──
# Примечание: сейчас лежит в spike/ (временная директория по плану Фазы 0);
# в Фазе 1 у tools/service_tui/ появляется tests/ — тогда этот файл стоит
# туда перенести (или вынести тесты в отдельный test_hab_golden.py).


def test_golden_hab_no_dcd(tmp_path: Path) -> None:
    api_bytes, cli_bytes = _run_golden(tmp_path, dcd_bin=None)
    assert api_bytes == cli_bytes, (
        f"HabImage API разошёлся с nxpimage CLI (DCD off): "
        f"{len(api_bytes)} vs {len(cli_bytes)} байт"
    )


def test_golden_hab_with_dcd(tmp_path: Path) -> None:
    if not REAL_DCD_BIN.exists():
        pytest.skip(f"Реальный DCD не найден: {REAL_DCD_BIN}")
    api_bytes, cli_bytes = _run_golden(tmp_path, dcd_bin=REAL_DCD_BIN)
    assert api_bytes == cli_bytes, (
        f"HabImage API разошёлся с nxpimage CLI (DCD on): "
        f"{len(api_bytes)} vs {len(cli_bytes)} байт"
    )


# ── standalone запуск: то же самое, но с подробным выводом в консоль ────


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--dcd-bin", type=Path, default=REAL_DCD_BIN, help="Путь к реальному dcd.bin"
    )
    args = parser.parse_args()

    try:
        _require_nxpimage()
    except RuntimeError as exc:
        print(f"❌ {exc}")
        return 1

    work_dir = Path(tempfile.mkdtemp(prefix="spike_hab_"))
    print(f"Рабочая директория: {work_dir}")

    ok = True
    for dcd_bin, label in [(None, "DCD off"), (args.dcd_bin, "DCD on")]:
        print(f"\n--- {label} ---")
        if dcd_bin is not None and not dcd_bin.exists():
            print(f"  ПРОПУЩЕНО: DCD-файл не найден: {dcd_bin}")
            continue
        try:
            api_bytes, cli_bytes = _run_golden(work_dir, dcd_bin)
            match = api_bytes == cli_bytes
            print(f"  API: {len(api_bytes)} байт, CLI: {len(cli_bytes)} байт")
            print(f"  {'✅ ПОБАЙТНО СОВПАДАЕТ' if match else '❌ РАСХОЖДЕНИЕ'}")
            ok = ok and match
        except subprocess.CalledProcessError as exc:
            print(f"  ❌ nxpimage CLI упал: {exc.stderr}")
            ok = False
        except Exception as exc:  # noqa: BLE001 — спайк, важен весь контекст ошибки
            print(f"  ❌ {type(exc).__name__}: {exc}")
            ok = False

    print(f"\nРезультат: {'✅ Гейт 0 (HAB) пройден' if ok else '❌ Стоп-условие В1'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
