# -*- mode: python ; coding: utf-8 -*-
"""
service_tui.spec — PyInstaller spec для service-tui (Фаза 5, RELEASE_ROADMAP.md).

Собирает onedir-бандл (не onefile — onefile замедляет старт распаковкой,
см. MONOLITH_APP_PLAN.md §Фаза 5). Итоговая структура:

    dist/service_tui/
    ├── service_tui[.exe]
    ├── _internal/
    │   ├── data/         ← dcd.bin, *_fdcb.bin, ivt_flashloader.bin, spsdk data
    │   └── ...           ← рантайм PyInstaller, libusbsio (из Analysis)
    ├── firmware/         ← НЕ создаётся этим spec — см. just-рецепт (post-build copy,
    │                        firmware_hab_path() во frozen ждёт его рядом с exe,
    │                        а не внутри _internal — PyInstaller datas всегда
    │                        кладёт файлы внутрь _internal, сюда достать не может)
    └── custom_binaries/  ← создаётся приложением само при первом запуске
                             (flasher.py::_resolve_custom_binaries_dir), не этим spec

Запуск: uv run --directory tools/production pyinstaller service_tui.spec
(все относительные пути ниже считаются от расположения этого файла — SPECPATH).
"""

import sys
from pathlib import Path

from PyInstaller.utils.hooks import collect_data_files, collect_dynamic_libs

_SPEC_DIR = Path(SPECPATH)  # tools/production/ — переменная предоставлена PyInstaller
_REPO_ROOT = _SPEC_DIR.parents[1]  # tools/production -> tools -> корень репозитория
_DCD_DIR = _REPO_ROOT / "tools" / "host" / "dcd"

# Иконка .exe — только Windows: на macOS без обёртки в BUNDLE() (.app) icon=
# у EXE() не влияет на отображение в Finder для голого консольного бинарника.
_WINDOWS_ICON = str(_SPEC_DIR / "assets" / "service_tui.ico") if sys.platform.startswith("win") else None

# ── datas ────────────────────────────────────────────────────────────────

# spsdk: ~380 файлов данных (data/devices/*/database.yaml и т.п.) — проверено,
# реально нужны (HabImage/Config резолвят family="mimxrt1050" через них),
# не декоративная предосторожность.
datas = collect_data_files("spsdk")

# app.tcss — Textual резолвит CSS_PATH относительно __file__ модуля, где
# определён App (app/app.py); во frozen эта директория виртуальна, но должна
# физически существовать в бандле по тому же относительному пути.
datas += [(str(_SPEC_DIR / "app" / "app.tcss"), "app")]

# pyproject.toml — для _read_app_version() (tomllib) во frozen; waiting.py
# резолвит parents[2] от __file__, что во frozen указывает на корень бандла.
datas += [(str(_SPEC_DIR / "pyproject.toml"), ".")]

# tools/host/dcd/*.bin — единый источник (Р6), тот же, что использует
# нетронутый flash_usb.py. Кладём в data/ внутри _internal — резолвер
# flash_backend.py (см. правку в этом же ответе) ждёт их именно там.
#
# Обязательные блобы: без них штатная и custom-прошивка молча ломаются
# в рантайме у сервисного инженера (frozen-резолвер найдёт пустую data/),
# поэтому отсутствие хотя бы одного — фатальная ошибка сборки, не warning.
_REQUIRED_DCD_BLOBS = (
    "dcd.bin",
    "ivt_flashloader.bin",
    "w25q128_fdcb.bin",
    "w25q512_fdcb.bin",
)
_missing_dcd_blobs = [
    name for name in _REQUIRED_DCD_BLOBS if not (_DCD_DIR / name).is_file()
]
if _missing_dcd_blobs:
    raise FileNotFoundError(
        f"Отсутствуют обязательные файлы в {_DCD_DIR}: {_missing_dcd_blobs}"
    )

for _blob in sorted(_DCD_DIR.glob("*.bin")):
    datas.append((str(_blob), "data"))

# ── binaries ─────────────────────────────────────────────────────────────

# libusbsio: нативный HID-транспорт (следствие Р7); libusb-1.0.* в бандле
# отсутствует. Заберёт бинарники всех платформ (см. пояснение в ответе,
# где предложен этот spec) — раздувает бандл, но не ломает.
binaries = collect_dynamic_libs("libusbsio")

# ── hiddenimports ────────────────────────────────────────────────────────

# app/ — implicit namespace package (нет __init__.py, в отличие от
# app/screens/). Перечисляем явно — цена нулевая, класс "тихо потерянного
# модуля" снимается целиком.
hiddenimports = [
    "app",
    "app.app",
    "app.screens",
    "app.widgets",
]

a = Analysis(
    [str(_SPEC_DIR / "main.py")],
    pathex=[str(_SPEC_DIR)],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="service_tui",
    debug=False,
    strip=False,
    upx=False,  # UPX + нативные HID-либы (libusbsio) — известный источник
                # проблем с загрузкой; отключаю явно, не полагаюсь на дефолт
    console=True,  # Textual — терминальное приложение, без консоли не отрисуется
    icon=_WINDOWS_ICON,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name="service_tui",
)
