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
# =============================================================================

[private]
_configure-debug:
    cmake --preset Debug

[private]
_configure-release:
    cmake --preset Release

[private]
_configure-target-debug:
    cmake --preset target-debug

# ── firmware_test ─────────────────────────────────────────────────────────────

[doc('Собрать firmware_test (Debug)')]
[group('build')]
build-firmware-test-debug: _configure-debug
    cmake --build --preset firmware-test-debug

[doc('Собрать firmware_test (Release)')]
[group('build')]
build-firmware-test-release: _configure-release
    cmake --build --preset firmware-test-release

# ── bootloader ───────────────────────────────────────────────────────────────

[doc('Собрать bootloader (Debug)')]
[group('build')]
build-bootloader-debug: _configure-debug
    cmake --build --preset bootloader-debug

[doc('Собрать bootloader (Release)')]
[group('build')]
build-bootloader-release: _configure-release
    cmake --build --preset bootloader-release

# ── app ───────────────────────────────────────────────────────────────────────

[doc('Собрать tft_app (Debug)')]
[group('build')]
build-app-debug: _configure-debug
    cmake --build --preset app-debug

[doc('Собрать tft_app (Release)')]
[group('build')]
build-app-release: _configure-release
    cmake --build --preset app-release

# ── все проекты сразу ─────────────────────────────────────────────────────────

[doc('Собрать все проекты (Debug)')]
[group('build')]
build-all-debug: _configure-debug
    cmake --build --preset all-debug

[doc('Собрать все проекты (Release)')]
[group('build')]
build-all-release: _configure-release
    cmake --build --preset all-release

# ── утилиты ───────────────────────────────────────────────────────────────────

[confirm("Delete build/ directory?")]
[doc('Удалить директорию build/')]
[group('build')]
clean:
    rm -rf build

# =============================================================================
# ГРУППА: test — host-тесты (Unity + fff, в devcontainer)
# =============================================================================

[private]
_configure-host-debug:
    cmake --preset host-debug

[private]
_configure-host-release:
    cmake --preset host-release

[doc('Собрать и запустить host unit-тесты (Debug)')]
[group('test')]
test-host: _configure-host-debug
    cmake --build --preset host-debug-build
    ctest --preset host-debug-test

[doc('Собрать и запустить host unit-тесты (Release)')]
[group('test')]
test-host-release: _configure-host-release
    cmake --build --preset host-release-build
    ctest --preset host-release-test

# =============================================================================
# ГРУППА: hil — сборка target-прошивок для HIL-тестов
# Запуск самих тестов — на хосте через just host::hil-*
# =============================================================================

[doc('Собрать все HIL target-прошивки (Debug)')]
[group('hil')]
build-hil: _configure-target-debug
    cmake --build --preset target-debug-build

# =============================================================================
# ГРУППА: hab_image_gen — подготовка HAB-образов
# =============================================================================
# ── firmware_test ─────────────────────────────────────────────────────────────

[doc('Собрать HAB-образ firmware_test (Debug)')]
[group('hab_image_gen')]
hab-firmware-test-debug: build-firmware-test-debug
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p "{{ BUILD_DIR }}/Debug"
    cd "{{ TOOLS_DIR }}/hab"
    uv run nxpimage hab export --force \
        -c hab_firmware_test_debug.yaml \
        -o "{{ BUILD_DIR }}/Debug/firmware_test_hab.bin"
    echo "  ✅  firmware_test_hab.bin (Debug)"

[doc('Собрать HAB-образ firmware_test (Release)')]
[group('hab_image_gen')]
hab-firmware-test-release: build-firmware-test-release
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p "{{ BUILD_DIR }}/Release"
    cd "{{ TOOLS_DIR }}/hab"
    uv run nxpimage hab export --force \
        -c hab_firmware_test_release.yaml \
        -o "{{ BUILD_DIR }}/Release/firmware_test_hab.bin"
    echo "  ✅  firmware_test_hab.bin (Release)"

# ── bootloader ───────────────────────────────────────────────────────────────

[doc('Собрать HAB-образ bootloader (Debug)')]
[group('hab_image_gen')]
hab-bootloader-debug: build-bootloader-debug
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p "{{ BUILD_DIR }}/Debug"
    cd "{{ TOOLS_DIR }}/hab"
    uv run nxpimage hab export --force \
        -c hab_bootloader_debug.yaml \
        -o "{{ BUILD_DIR }}/Debug/bootloader_hab.bin"
    echo "  ✅  bootloader_hab.bin (Debug)"

[doc('Собрать HAB-образ bootloader (Release)')]
[group('hab_image_gen')]
hab-bootloader-release: build-bootloader-release
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p "{{ BUILD_DIR }}/Release"
    cd "{{ TOOLS_DIR }}/hab"
    uv run nxpimage hab export --force \
        -c hab_bootloader_release.yaml \
        -o "{{ BUILD_DIR }}/Release/bootloader_hab.bin"
    echo "  ✅  bootloader_hab.bin (Release)"

# ── app ───────────────────────────────────────────────────────────────────────

[doc('Собрать HAB-образ tft_app (Debug)')]
[group('hab_image_gen')]
hab-app-debug: build-app-debug
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p "{{ BUILD_DIR }}/Debug"
    cd "{{ TOOLS_DIR }}/hab"
    uv run nxpimage hab export --force \
        -c hab_app_debug.yaml \
        -o "{{ BUILD_DIR }}/Debug/app_hab.bin"
    echo "  ✅  app_hab.bin (Debug)"

[doc('Собрать HAB-образ tft_app (Release)')]
[group('hab_image_gen')]
hab-app-release: build-app-release
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p "{{ BUILD_DIR }}/Release"
    cd "{{ TOOLS_DIR }}/hab"
    uv run nxpimage hab export --force \
        -c hab_app_release.yaml \
        -o "{{ BUILD_DIR }}/Release/app_hab.bin"
    echo "  ✅  app_hab.bin (Release)"

# ── все образы сразу ─────────────────────────────────────────────────────────

[doc('Собрать HAB-образы всех проектов (Debug)')]
[group('hab_image_gen')]
hab-all-debug: hab-firmware-test-debug hab-bootloader-debug hab-app-debug
    @echo "  ✅  All HAB images (Debug) ready"

[doc('Собрать HAB-образы всех проектов (Release)')]
[group('hab_image_gen')]
hab-all-release: hab-firmware-test-release hab-bootloader-release hab-app-release
    @echo "  ✅  All HAB images (Release) ready"

# ── верификация образа ────────────────────────────────────────────────────────

[doc('Верифицировать HAB-образ: hab-verify <project> <debug|release>')]
[group('hab_image_gen')]
hab-verify project="firmware_test" type="release":
    #!/usr/bin/env bash
    set -euo pipefail
    case "{{ project }}-{{ type }}" in
        firmware_test-debug)   BIN="{{ BUILD_DIR }}/Debug/firmware_test_hab.bin" ;;
        firmware_test-release) BIN="{{ BUILD_DIR }}/Release/firmware_test_hab.bin" ;;
        bootloader-debug)      BIN="{{ BUILD_DIR }}/Debug/bootloader_hab.bin" ;;
        bootloader-release)    BIN="{{ BUILD_DIR }}/Release/bootloader_hab.bin" ;;
        app-debug)             BIN="{{ BUILD_DIR }}/Debug/app_hab.bin" ;;
        app-release)           BIN="{{ BUILD_DIR }}/Release/app_hab.bin" ;;
        *)
            echo "  ❌  Unknown: {{ project }}-{{ type }}"
            exit 1 ;;
    esac
    OUT="/tmp/hab_parse_{{ project }}_{{ type }}.yaml"
    cd "{{ TOOLS_DIR }}" && uv run nxpimage hab parse -b "${BIN}" -o "${OUT}"
    echo "  ✅  ${OUT}"
    grep -E "(entry|csf|tag)" "${OUT}" || true

# =============================================================================
# ГРУППА: quality
# =============================================================================

[doc('Запустить линтеры (clang-tidy)')]
[group('quality')]
lint:
    echo "TODO!"

[doc('Форматировать код (clang-format)')]
[group('quality')]
format:
    echo "TODO!"
