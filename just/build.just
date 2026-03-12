# =============================================================================
# scripts/build.just — сборка, тесты, HAB-образы
# Выполняется ТОЛЬКО внутри devcontainer (из терминала VSCode или tasks).
#
# Рабочая директория — корень репозитория (не scripts/).
# =============================================================================

# === Импорт общих переменных ===
TOOLS_DIR := env('TOOLS_DIR', justfile_directory() / 'tools/host')
BUILD_DIR := env('BUILD_DIR', justfile_directory() / 'build')

set working-directory := '..'

_default:
    @just --list --justfile scripts/build.just --unsorted

# =============================================================================
# ГРУППА: build
#
# Матрица сборки:
#   firmware-test  × debug / release
#   bootloader     × debug / release
#   app            × debug / release
#   all            × debug / release  (все проекты сразу, для CI)
# =============================================================================
# Приватные рецепты для configure (не показываются в --list)
[private]
_configure-debug:
    cmake --preset Debug

[private]
_configure-release:
    cmake --preset Release

# ── firmware_test ─────────────────────────────────────────────────────────────
[group('build')]
build-firmware-test-debug: _configure-debug
    cmake --build --preset firmware-test-debug

[group('build')]
build-firmware-test-release: _configure-release
    cmake --build --preset firmware-test-release

# ── bootloader ───────────────────────────────────────────────────────────────
[group('build')]
build-bootloader-debug: _configure-debug
    cmake --build --preset bootloader-debug

[group('build')]
build-bootloader-release: _configure-release
    cmake --build --preset bootloader-release

# ── app ───────────────────────────────────────────────────────────────────────
[group('build')]
build-app-debug: _configure-debug
    cmake --build --preset app-debug

[group('build')]
build-app-release: _configure-release
    cmake --build --preset app-release

# ── все проекты сразу ─────────────────────────────────────────────────────────
[group('build')]
build-all-debug: _configure-debug
    cmake --build --preset all-debug

[group('build')]
build-all-release: _configure-release
    cmake --build --preset all-release

# ── утилиты ───────────────────────────────────────────────────────────────────
[group('build'), confirm("Delete build/ directory?")]
clean:
    rm -rf build

# =============================================================================
# ГРУППА: test
# =============================================================================

[private]
_configure-host-debug:
    cmake --preset host-debug

[private]
_configure-host-release:
    cmake --preset host-release

[group('test')]
test-host: _configure-host-debug
    cmake --build --preset host-debug-build
    ctest --preset host-debug-test

[group('test')]
test-host-release: _configure-host-release
    cmake --build --preset host-release-build
    ctest --preset host-release-test

# =============================================================================
# ГРУППА: flash — подготовка HAB-образов
#
# Матрица HAB:
#   firmware-test  × debug / release
#   bootloader     × debug / release
#   app            × debug / release
#   all            × debug / release
#
# Debug HAB — для прошивки своей платы и отладки.
# Release HAB — для производственного сервера.
#
# HAB yaml конфиги:
#   tools/host/hab/hab_<project>_debug.yaml    — путь к Debug ELF
#   tools/host/hab/hab_<project>_release.yaml  — путь к Release ELF
# =============================================================================

# ── firmware_test ─────────────────────────────────────────────────────────────
[group('hab_image_gen')]
hab-firmware-test-debug: build-firmware-test-debug
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{TOOLS_DIR}}/hab"
    uv run nxpimage hab export --force \
        -c hab_firmware_test_debug.yaml \
        -o "../../../{{BUILD_DIR}}/Debug/firmware_test_hab.bin"
    echo "  ✅  firmware_test_hab.bin (Debug)"

[group('hab_image_gen')]
hab-firmware-test-release: build-firmware-test-release
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{TOOLS_DIR}}/hab"
    uv run nxpimage hab export --force \
        -c hab_firmware_test_release.yaml \
        -o "../../../{{BUILD_DIR}}/Release/firmware_test_hab.bin"
    echo "  ✅  firmware_test_hab.bin (Release)"

# ── bootloader ───────────────────────────────────────────────────────────────
[group('hab_image_gen')]
hab-bootloader-debug: build-bootloader-debug
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{TOOLS_DIR}}/hab"
    uv run nxpimage hab export --force \
        -c hab_bootloader_debug.yaml \
        -o "../../../{{BUILD_DIR}}/Debug/bootloader_hab.bin"
    echo "  ✅  bootloader_hab.bin (Debug)"

[group('hab_image_gen')]
hab-bootloader-release: build-bootloader-release
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{TOOLS_DIR}}/hab"
    uv run nxpimage hab export --force \
        -c hab_bootloader_release.yaml \
        -o "../../../{{BUILD_DIR}}/Release/bootloader_hab.bin"
    echo "  ✅  bootloader_hab.bin (Release)"

# ── app ───────────────────────────────────────────────────────────────────────
[group('hab_image_gen')]
hab-app-debug: build-app-debug
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{TOOLS_DIR}}/hab"
    uv run nxpimage hab export --force \
        -c hab_app_debug.yaml \
        -o "../../../{{BUILD_DIR}}/Debug/app_hab.bin"
    echo "  ✅  app_hab.bin (Debug)"

[group('hab_image_gen')]
hab-app-release: build-app-release
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{TOOLS_DIR}}/hab"
    uv run nxpimage hab export --force \
        -c hab_app_release.yaml \
        -o "../../../{{BUILD_DIR}}/Release/app_hab.bin"
    echo "  ✅  app_hab.bin (Release)"

# ── все образы сразу ─────────────────────────────────────────────────────────
[group('hab_image_gen')]
hab-all-debug: hab-firmware-test-debug hab-bootloader-debug hab-app-debug
    @echo "  ✅  All HAB images (Debug) ready"

[group('hab_image_gen')]
hab-all-release: hab-firmware-test-release hab-bootloader-release hab-app-release
    @echo "  ✅  All HAB images (Release) ready"

# ── верификация образа (для отладки) ─────────────────────────────────────────
# Использование: just --justfile just/build.just hab-verify firmware_test debug
[group('hab_image_gen')]
hab-verify project="firmware_test" type="release":
    #!/usr/bin/env bash
    set -euo pipefail
    ROOT="$(pwd)"
    case "{{project}}-{{type}}" in
        firmware_test-debug)   BIN="${ROOT}/{{BUILD_DIR}}/Debug/firmware_test_hab.bin" ;;
        firmware_test-release) BIN="${ROOT}/{{BUILD_DIR}}/Release/firmware_test_hab.bin" ;;
        bootloader-debug)      BIN="${ROOT}/{{BUILD_DIR}}/Debug/bootloader_hab.bin" ;;
        bootloader-release)    BIN="${ROOT}/{{BUILD_DIR}}/Release/bootloader_hab.bin" ;;
        app-debug)             BIN="${ROOT}/{{BUILD_DIR}}/Debug/app_hab.bin" ;;
        app-release)           BIN="${ROOT}/{{BUILD_DIR}}/Release/app_hab.bin" ;;
        *)
            echo "  ❌  Unknown: {{project}}-{{type}}"
            echo "  Projects: firmware_test, bootloader, app"
            echo "  Types:    debug, release"
            exit 1 ;;
    esac
    OUT="/tmp/hab_parse_{{project}}_{{type}}.yaml"
    cd "{{TOOLS_DIR}}" && uv run  nxpimage hab parse -b "${BIN}" -o "${OUT}"
    echo "  ✅  ${OUT}"
    grep -E "(entry|csf|tag)" "${OUT}" || true

# =============================================================================
# ГРУППА: quality — линтеры, форматтеры
# =============================================================================
# TODO: проверять bsp,firmware, modules
[group('quality')]
lint:
    echo "TODO!"
# clang-tidy -p {{BUILD_DIR}}/Debug src/**/*.c

[group('quality')]
format:
    echo "TODO!"
# clang-format -i src/**/*.{c,h}
