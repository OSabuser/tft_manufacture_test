# =============================================================================
# host.just — операции на хост-машине (вне devcontainer)
# =============================================================================

TOOLS_DIR := env('TOOLS_DIR', justfile_directory() / 'tools/host')
HIL_DIR := justfile_directory() / 'tools/hil'
CACHE_DIR := env('CACHE_DIR', justfile_directory() / '.cache')
BUILD_DIR := env('BUILD_DIR', justfile_directory() / 'build')
_uname := `uname -s`
_venv := if _uname =~ "MINGW|MSYS|CYGWIN" { ".venv-host-win" } else { ".venv-host" }
JUST_MIN := "1.36.0"
UV_MIN := "0.4.0"
DOCKER_MIN := "24.0.0"
PLATFORM := os()

set working-directory := '..'

# =============================================================================
# ГРУППА: setup
# =============================================================================

[doc('Проверить версии и наличие зависимостей хоста (just, uv, docker)')]
[group('setup')]
check-deps:
    #!/usr/bin/env bash
    set -euo pipefail

    BOLD="\033[1m"; GREEN="\033[0;32m"; RED="\033[0;31m"; YELLOW="\033[1;33m"; RESET="\033[0m"
    ERRORS=0; WARNINGS=0

    ok()   { echo -e "  ${GREEN}✅  ${1}${RESET}"; }
    fail() { echo -e "  ${RED}❌  ${1}${RESET}"; ERRORS=$((ERRORS+1)); }
    warn() { echo -e "  ${YELLOW}⚠️   ${1}${RESET}"; WARNINGS=$((WARNINGS+1)); }

    semver_ge() {
        local a="$1" b="$2"
        local a1 a2 a3 b1 b2 b3
        IFS='.' read -r a1 a2 a3 <<< "${a}"
        IFS='.' read -r b1 b2 b3 <<< "${b}"
        a1="${a1//[^0-9]/}"; a2="${a2//[^0-9]/}"; a3="${a3//[^0-9]/}"
        b1="${b1//[^0-9]/}"; b2="${b2//[^0-9]/}"; b3="${b3//[^0-9]/}"
        [[ "${a1:-0}" -gt "${b1:-0}" ]] && return 0
        [[ "${a1:-0}" -lt "${b1:-0}" ]] && return 1
        [[ "${a2:-0}" -gt "${b2:-0}" ]] && return 0
        [[ "${a2:-0}" -lt "${b2:-0}" ]] && return 1
        [[ "${a3:-0}" -ge "${b3:-0}" ]] && return 0
        return 1
    }

    install_hint() {
        local tool=$1
        case "{{ PLATFORM }}" in
            linux)
                case "$tool" in
                    just)   echo "uv tool install 'rust-just=={{ JUST_MIN }}'" ;;
                    uv)     echo "curl -LsSf https://astral.sh/uv/install.sh | sh" ;;
                    docker) echo "https://docs.docker.com/engine/install/ubuntu/" ;;
                esac ;;
            macos)
                case "$tool" in
                    just)   echo "uv tool install 'rust-just=={{ JUST_MIN }}'" ;;
                    uv)     echo "brew install uv" ;;
                    docker) echo "brew install --cask docker" ;;
                esac ;;
            windows)
                case "$tool" in
                    just)   echo "uv tool install 'rust-just=={{ JUST_MIN }}'" ;;
                    uv)     echo 'powershell -c "irm https://astral.sh/uv/install.ps1 | iex"' ;;
                    docker) echo "winget install --id Docker.DockerDesktop" ;;
                esac ;;
        esac
    }

    check_tool() {
        local name=$1 cmd=$2 min_ver=$3
        shift 3
        local ver_args=("${@:---version}")
        if ! command -v "$cmd" &>/dev/null; then
            fail "$name not found -- $(install_hint "$name")"
            return
        fi
        local actual
        actual=$("$cmd" "${ver_args[@]}" 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        if [[ -z "$actual" ]]; then
            warn "$name found but version could not be detected"
            return
        fi
        if semver_ge "$actual" "$min_ver"; then
            ok "$name $actual (>= $min_ver)"
        else
            fail "$name $actual -- need >= $min_ver -- $(install_hint "$name")"
        fi
    }

    echo ""
    echo -e "${BOLD}  Platform: {{ PLATFORM }}${RESET}"
    echo ""
    echo -e "${BOLD}  Checking host dependencies...${RESET}"
    echo ""

    check_tool "just"   "just"   "{{ JUST_MIN }}"
    check_tool "uv"     "uv"     "{{ UV_MIN }}"
    check_tool "docker" "docker" "{{ DOCKER_MIN }}"

    if [[ "{{ PLATFORM }}" == "linux" ]]; then
        echo ""
        echo -e "${BOLD}  Checking udev rules (Linux only)...${RESET}"
        echo ""
        rules_file="/etc/udev/rules.d/99-nxp-mimxrt.rules"
        udev_ok=true
        grep -q "1fc9" "$rules_file" 2>/dev/null || { warn "udev: BootROM rule missing"; udev_ok=false; }
        grep -q "15a2" "$rules_file" 2>/dev/null || { warn "udev: Flashloader rule missing"; udev_ok=false; }
        if ! groups 2>/dev/null | grep -q plugdev; then
            warn "User '$(whoami)' not in 'plugdev' group"
            udev_ok=false
        fi
        if [[ "$udev_ok" == "false" ]]; then
            warn "Run: just host::setup-udev"
        else
            ok "udev rules (NXP USB) + plugdev group"
        fi
    fi

    echo ""
    if [[ $ERRORS -gt 0 ]]; then
        echo -e "  ${RED}${BOLD}$ERRORS error(s) found.${RESET}"
        exit 1
    elif [[ $WARNINGS -gt 0 ]]; then
        echo -e "  ${YELLOW}${BOLD}$WARNINGS warning(s).${RESET}"
    else
        echo -e "  ${GREEN}${BOLD}All dependencies satisfied.${RESET}"
    fi
    echo ""

[doc('Установить udev rules для NXP USB (только Linux)')]
[group('setup')]
setup-udev:
    #!/usr/bin/env bash
    set -euo pipefail
    case "$(uname -s)" in
        Linux*) ;;
        *) echo "  ℹ️  udev rules are Linux-only, skipping."; exit 0 ;;
    esac
    printf '%s\n%s\n%s\n%s\n' \
        '# NXP BootROM -- SDP mode' \
        'SUBSYSTEM=="usb", ATTR{idVendor}=="1fc9", ATTR{idProduct}=="0130", MODE="0666", GROUP="plugdev"' \
        '# NXP Flashloader' \
        'SUBSYSTEM=="usb", ATTR{idVendor}=="15a2", ATTR{idProduct}=="0073", MODE="0666", GROUP="plugdev"' \
        | sudo tee /etc/udev/rules.d/99-nxp-mimxrt.rules > /dev/null
    sudo udevadm control --reload-rules && sudo udevadm trigger
    sudo usermod -a -G plugdev "$USER"
    echo "  ✅  udev rules installed. Re-login required."

[doc('Установить/обновить Python-зависимости tools/host (spsdk и др.)')]
[group('setup')]
setup-tools:
    #!/usr/bin/env bash
    set -euo pipefail
    LOCK="{{ TOOLS_DIR }}/uv.lock"
    STAMP="{{ CACHE_DIR }}/uv_lock.sha256"
    mkdir -p "{{ CACHE_DIR }}"
    current=$(sha256sum "$LOCK" 2>/dev/null | cut -d' ' -f1 || echo "missing")
    previous=$(cat "$STAMP" 2>/dev/null || echo "none")
    if [[ "$current" == "$previous" ]]; then
        echo "  ✅  tools/host deps up to date"
    else
        echo "  📦  Syncing tools/host deps..."
        cd "{{ TOOLS_DIR }}" && uv sync
        echo "$current" > "$STAMP"
        echo "  ✅  tools/host deps installed"
    fi

[doc('Полная первичная настройка хоста: check-deps + setup-udev + setup-tools')]
[group('setup')]
bootstrap:
    #!/usr/bin/env bash
    set -euo pipefail

    echo ""
    echo "  === Bootstrap: step 1/3 -- check-deps ==="
    just host::check-deps
    if [[ "{{ PLATFORM }}" == "linux" ]]; then
        echo "  === Bootstrap: step 2/3 -- setup-udev ==="
        just host::setup-udev
        just host::setup-m5-udev
    else
        echo "  ℹ️  step 2/3 -- setup-udev skipped ({{ PLATFORM }})"
    fi
    echo "  === Bootstrap: step 3/3 -- setup-tools ==="
    just host::setup-tools
    echo ""
    echo "  ✅  Bootstrap complete."
    echo "  Next: open project in VSCode -> 'Reopen in Container'"
    echo ""

[doc('Добавить пользователя в группу dialout (для /dev/ttyACM* — M5Stack, Linux)')]
[group('setup')]
setup-m5-udev:
    #!/usr/bin/env bash
    set -euo pipefail
    case "$(uname -s)" in
        Linux*) ;;
        *) echo "  ℹ️  dialout group — Linux only, skipping."; exit 0 ;;
    esac
    if groups "$USER" | grep -q dialout; then
        echo "  ✅  User '${USER}' already in dialout"
    else
        sudo usermod -a -G dialout "$USER"
        echo "  ✅  Added '${USER}' to dialout. Re-login required."
    fi

# =============================================================================
# ГРУППА: flash-sdp — USB Serial Downloader (SDP через ROM-загрузчик)
# Требование: плата в режиме Serial Downloader (BOOT_MODE = 01)
# =============================================================================

[doc('Прошить образ через USB SDP: flash <firmware_test|bootloader|app> <debug|release>')]
[group('flash-sdp')]
flash project type="release":
    #!/usr/bin/env bash
    set -euo pipefail
    case "{{ type }}" in
        debug)   BUILD_TYPE="Debug" ;;
        release) BUILD_TYPE="Release" ;;
        *) echo "  ❌  Unknown type: {{ type }}"; exit 1 ;;
    esac
    cd "{{ TOOLS_DIR }}" && UV_PROJECT_ENVIRONMENT={{ _venv }} uv run python flash_usb.py \
        --firmware "{{ project }}" --build-type "${BUILD_TYPE}"

[doc('Прошить образ в RAM через USB SDP: flash-ram <firmware_test|bootloader|app> <debug|release>')]
[group('flash-sdp')]
flash-ram project type="debug":
    #!/usr/bin/env bash
    set -euo pipefail
    case "{{ type }}" in
        debug)   BUILD_TYPE="Debug" ;;
        release) BUILD_TYPE="Release" ;;
        *) echo "  ❌  Unknown type: {{ type }}"; exit 1 ;;
    esac
    cd "{{ TOOLS_DIR }}" && UV_PROJECT_ENVIRONMENT={{ _venv }} uv run python flash_usb.py \
        --firmware "{{ project }}" --build-type "${BUILD_TYPE}" --ram-only

[doc('Прошить firmware_test Debug через USB SDP')]
[group('flash-sdp')]
flash-test-debug: (flash "firmware_test" "debug")

[doc('Прошить firmware_test Release через USB SDP')]
[group('flash-sdp')]
flash-test-release: (flash "firmware_test" "release")

[confirm("Flash bootloader + app (Release)?")]
[doc('Прошить bootloader + tft_app Release через USB SDP (производство)')]
[group('flash-sdp')]
flash-production: (flash "bootloader" "release") (flash "app" "release")

[confirm("Стереть всю Flash (W25Q128) через USB SDP? Все прошивки будут удалены.")]
[doc('Стереть всю Flash (W25Q128) через USB SDP (chip erase, ~30 с)')]
[group('flash-sdp')]
flash-sdp-erase:
    cd "{{ TOOLS_DIR }}" && UV_PROJECT_ENVIRONMENT={{ _venv }} uv run python flash_usb.py \
        --erase-chip

# =============================================================================
# ГРУППА: flash-swd — прошивка через SWD (MCU-Link, без смены BOOT_MODE)
# =============================================================================

_flash_swd := TOOLS_DIR / "flash_swd.py"

[doc('Прошить firmware_test Debug через SWD. Нужен power cycle после')]
[group('flash-swd')]
flash-swd-test-debug:
    uv run --directory {{ HIL_DIR }} python {{ _flash_swd }} \
        --firmware firmware_test --build-type Debug

[doc('Прошить firmware_test Release через SWD. Нужен power cycle после')]
[group('flash-swd')]
flash-swd-test-release:
    uv run --directory {{ HIL_DIR }} python {{ _flash_swd }} \
        --firmware firmware_test --build-type Release

[doc('Прошить bootloader Debug через SWD. Нужен power cycle после')]
[group('flash-swd')]
flash-swd-bootloader-debug:
    uv run --directory {{ HIL_DIR }} python {{ _flash_swd }} \
        --firmware bootloader --build-type Debug

[doc('Прошить bootloader Release через SWD. Нужен power cycle после')]
[group('flash-swd')]
flash-swd-bootloader-release:
    uv run --directory {{ HIL_DIR }} python {{ _flash_swd }} \
        --firmware bootloader --build-type Release

[doc('Прошить tft_app Debug через SWD. Нужен power cycle после')]
[group('flash-swd')]
flash-swd-app-debug:
    uv run --directory {{ HIL_DIR }} python {{ _flash_swd }} \
        --firmware app --build-type Debug

[doc('Прошить tft_app Release через SWD. Нужен power cycle после')]
[group('flash-swd')]
flash-swd-app-release:
    uv run --directory {{ HIL_DIR }} python {{ _flash_swd }} \
        --firmware app --build-type Release

[confirm("Стереть всю Flash (W25Q128)? Все прошивки будут удалены.")]
[doc('Стереть всю Flash (W25Q128) через SWD (chip erase). Нужен power cycle после')]
[group('flash-swd')]
flash-swd-erase:
    uv run --directory {{ HIL_DIR }} pyocd erase \
        --target {{ env('PYOCD_TARGET', 'mimxrt1050_quadspi') }} \
        --frequency {{ env('PYOCD_FREQUENCY', '4000000') }} \
        --chip -O resume_on_disconnect=False

# =============================================================================
# ГРУППА: hil — запуск HIL-тестов на реальном железе
# Сборка target-прошивок — в devcontainer: just build::build-hil
# =============================================================================

_hil_build := BUILD_DIR / "target-debug"

[doc('Запустить все не интерактивные HIL-тесты')]
[group('hil')]
hil-run:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest -m "not interactive and not usb_vcom" -v 

[doc('Запустить все интерактивные HIL-тесты: кнопки, дисплей')]
[group('hil')]
hil-run-interactive:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest -m interactive -s -v

[doc('Запустить HIL-тест UART')]
[group('hil')]
hil-uart:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest 01_test_uart.py -v

[doc('Запустить HIL-тест оптоизолированных входов')]
[group('hil')]
hil-opto:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest 02_test_opto.py -v

[doc('Запустить HIL-тест кнопок')]
[group('hil')]
hil-button:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest 04_test_button.py -s -v

[doc('Запустить HIL тест bsp_can')]
[group('hil')]
hil-can:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest 03_test_can.py -v

[doc('Запустить HIL-тест USB CDC')]
[group('hil')]
hil-usb-cdc:
    HIL_BUILD_DIR={{ _hil_build }} \
        uv run --directory {{ HIL_DIR }} pytest 05_test_usb_cdc.py -v

[doc('Запустить HIL-тест оптовходов через firmware_test CDC (протокол v2)')]
[group('hil')]
hil-firmware-opto:
    uv run --directory {{ HIL_DIR }} pytest 06_test_firmware_opto.py -v

[doc('Запустить HIL-тест CAN через firmware_test CDC (протокол v2)')]
[group('hil')]
hil-firmware-can:
    uv run --directory {{ HIL_DIR }} pytest 06_test_firmware_can.py -v

[doc('Запустить все firmware_test CDC HIL-тесты (opto + can)')]
[group('hil')]
hil-firmware:
    uv run --directory {{ HIL_DIR }} pytest 06_test_firmware_opto.py 06_test_firmware_can.py -v

# =============================================================================
# ГРУППА: debug — GDB-сервер для отладки из VSCode (devcontainer)
#
# Рабочий цикл А — прошивка уже в Flash:
#   just host::debug-server  →  VSCode: 🐛 Debug: <project>  →  F5
#
# Рабочий цикл Б — прошить и отладить без USB SDP:
#   just host::flash-swd-test-debug
#   just host::debug-server          →  VSCode: 🐛 Debug: firmware_test  →  F5
#
# Важно: debug-server и flash-swd используют MCU-Link монопольно.
#        Остановите сервер (Ctrl+C) перед flash-swd и наоборот.
# =============================================================================

_hil_dir := "tools/hil"

[doc('Запустить GDB-сервер. Оставить запущенным, подключаться из VSCode')]
[group('debug')]
debug-server:
    uv run --directory {{ _hil_dir }} pyocd gdbserver \
        --config {{ justfile_directory() }}/pyocd_debug.yaml \
        --port {{ env('GDB_PORT', '3333') }} \
        --persist \
        --semihosting

[doc('Показать доступные builtin-таргеты pyOCD для MIMXRT')]
[group('debug')]
debug-list-targets:
    uv run --directory {{ _hil_dir }} pyocd list --targets | grep -i mimx

# =============================================================================
# ГРУППА: m5 — работа с M5Stack StamPLC
# =============================================================================

M5_PORT := env('HIL_M5_PORT', '/dev/ttyACM1')
_m5_agent := justfile_directory() / 'tools/hil/m5/agent.py'

[doc('Показать подключённые M5Stack устройства (USB CDC)')]
[group('m5')]
m5-scan:
    #!/usr/bin/env bash
    set -euo pipefail

    echo ""
    echo "  Сканируем устройства через mpremote..."
    echo ""

    devices=$(uv run --directory "{{ HIL_DIR }}" mpremote devs 2>&1)

    if [[ -z "$devices" ]]; then
        echo "  ⚠️  Устройства не найдены"
        echo "  Проверьте USB-кабель и права доступа к порту"
        echo "  Linux: sudo usermod -a -G dialout \$USER && newgrp dialout"
        echo "  macOS: никаких дополнительных прав не требуется"
        exit 1
    fi

    echo "$devices"
    echo ""

    # Подсветить известный VID:PID для Espressif (M5StamPLC JTAG/serial)
    if echo "$devices" | grep -qi "303a:1001"; then
        echo "  ✅  M5StamPLC (Espressif USB JTAG/serial) обнаружен"
    elif echo "$devices" | grep -qi "303a:"; then
        echo "  ✅  Espressif устройство обнаружено"
    else
        echo "  ⚠️  M5StamPLC не найден среди устройств"
    fi
    echo ""

[doc('Открыть MicroPython REPL на M5Stack (Ctrl+X для выхода)')]
[group('m5')]
m5-repl:
    uv run --directory {{ HIL_DIR }} mpremote connect {{ M5_PORT }} repl

[doc('Проверить версию MicroPython на M5Stack')]
[group('m5')]
m5-version:
    uv run --directory "{{ HIL_DIR }}" mpremote connect "{{ M5_PORT }}" \
        exec 'import sys; print("MicroPython:", sys.version); import machine; print("Freq:", machine.freq(), "Hz")'

[doc('Развернуть агент + запустить CLI одной командой (быстрый старт)')]
[group('m5')]
m5-start:
    #!/usr/bin/env bash
    set -euo pipefail

    just host::m5-deploy
    uv run --directory "{{ HIL_DIR }}" mpremote connect "{{ M5_PORT }}" reset
    sleep 2
    just host::m5-cli

[doc('Управление питанием таргета через M5 RLY1: m5-power <on|off>')]
[group('m5')]
m5-power state:
    uv run --directory {{ HIL_DIR }} python m5/power.py \
        --port {{ M5_PORT }} --state {{ state }}

[doc('Скопировать агент на M5Stack (tools/hil/m5_agent.py → /main.py)')]
[group('m5')]
m5-deploy:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📤  Копируем агент на M5Stack..."
    uv run --directory "{{ HIL_DIR }}" mpremote connect "{{ M5_PORT }}" \
        cp "{{ _m5_agent }}" :main.py
    uv run --directory "{{ HIL_DIR }}" mpremote connect "{{ M5_PORT }}" reset
    echo "  ✅  m5_agent.py → /main.py"
    echo "  ✅  M5Stack перезапущен"

[doc('Запустить интерактивный CLI для ручного тестирования агента')]
[group('m5')]
m5-cli:
    uv run --directory {{ HIL_DIR }} python m5/cli.py --port {{ M5_PORT }}

# =============================================================================
# ГРУППА: util
# =============================================================================

[doc('Найти подключённые NXP устройства (BootROM, flashloader, обычный режим)')]
[group('util')]
scan:
    cd "{{ TOOLS_DIR }}" && UV_PROJECT_ENVIRONMENT={{ _venv }} uv run nxpdevscan

[doc('Получить статус SDP (BootROM режим, VID:PID 1FC9:0130)')]
[group('util')]
[no-cd]
sdp-status:
    cd "{{ TOOLS_DIR }}" && UV_PROJECT_ENVIRONMENT={{ _venv }} uv run sdphost -u 0x1FC9,0x0130 -- error-status

[doc('Получить статус flashloader (VID:PID 15A2:0073)')]
[group('util')]
flashloader-status:
    cd "{{ TOOLS_DIR }}" && UV_PROJECT_ENVIRONMENT={{ _venv }} uv run blhost -u 0x15A2,0x0073 -- get-property 1 0

[doc('Открыть UART монитор на MCU-Link VCOM (кроссплатформенный)')]
[group('util')]
uart-monitor:
    uv run --directory {{ HIL_DIR }} python3 -m serial.tools.miniterm \
        {{ env('HIL_VCOM_PORT') }} {{ env('HIL_VCOM_BAUD', '115200') }} \
        --raw

# =============================================================================
# ГРУППА: pipeline
# =============================================================================

[doc('Входной контроль: прошить firmware_test Release + запустить HIL-тесты')]
[group('pipeline')]
incoming: flash-test-release
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  ▶  Running HIL tests..."
    just host::hil-run

[doc('Производственная прошивка: bootloader + tft_app Release')]
[group('pipeline')]
production: flash-production
    @echo "  ✅  Production firmware flashed"

# =============================================================================
# ГРУППА: service — TUI сервисного инженера
# =============================================================================

PRODUCTION_DIR := justfile_directory() / 'tools/production'

[doc('Установить/обновить зависимости TUI сервисного инженера')]
[group('service')]
service-setup:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📦  Syncing tools/production deps..."
    cd "{{ PRODUCTION_DIR }}" && uv sync
    echo "  ✅  tools/production deps installed"

[doc('Запустить TUI сервисного инженера')]
[group('service')]
service-tui:
    uv run --directory {{ PRODUCTION_DIR }} python main.py

[doc('Собрать standalone-бинарь TUI (PyInstaller → dist/service_tui)')]
[group('service')]
service-build:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  🔨  Building standalone service-tui..."
    cd "{{ PRODUCTION_DIR }}" && uv run pyinstaller \
        --onefile \
        --name service_tui \
        --add-data "../shared:shared" \
        main.py
    echo "  ✅  dist/service_tui ready"

# =============================================================================
# ГРУППА: package — сборка service-tui в исполняемый бандл (PyInstaller)
# =============================================================================

_prod_dir := justfile_directory() / 'tools/production'
_prod_build_dir := env('BUILD_DIR', justfile_directory() / 'build')

[doc('Собрать service-tui в PyInstaller-бандл + скопировать firmware/*_hab.bin')]
[group('package')]
package-tui:
    #!/usr/bin/env bash
    set -euo pipefail

    cd "{{ _prod_dir }}"
    echo "  📦  Running PyInstaller..."
    uv run pyinstaller service_tui.spec --noconfirm

    DIST="dist/service_tui"

    echo "  📦  Copying firmware HAB images (build/<Type>/*_hab.bin)..."
    found_debug=0
    for type_dir in "{{ _prod_build_dir }}"/Debug "{{ _prod_build_dir }}"/Release; do
        [[ -d "$type_dir" ]] || continue
        type_name=$(basename "$type_dir")
        for hab in "$type_dir"/*_hab.bin; do
            [[ -f "$hab" ]] || continue
            mkdir -p "$DIST/firmware/$type_name"
            cp "$hab" "$DIST/firmware/$type_name/"
            [[ "$type_name" == "Debug" ]] && found_debug=1
        done
    done
    if [[ "$found_debug" -eq 0 ]]; then
        echo "  ❌  Не найден Debug *_hab.bin в {{ _prod_build_dir }}/Debug — соберите"
        echo "      прошивку заранее (just build::hab-firmware-test-debug)."
        echo "      Бандл TUI без Debug-образа нерабочий (см. README/DEV_ARCH.md)."
        exit 1
    fi

    VERSION=$(grep -m1 '^version' pyproject.toml | sed -E 's/.*"(.+)".*/\1/')
    case "$(uname -s)" in
        Linux*)               OS_TAG="linux"   ;;
        Darwin*)              OS_TAG="macos"   ;;
        MINGW*|MSYS*|CYGWIN*) OS_TAG="windows" ;;
        *)                    OS_TAG="unknown" ;;
    esac
    RELEASE_NAME="service-tui-v${VERSION}-${OS_TAG}"

    rm -rf "dist/${RELEASE_NAME}"
    mkdir -p "dist/${RELEASE_NAME}"
    mv "$DIST" "dist/${RELEASE_NAME}/"

    echo "  📦  Seeding custom_binaries/ from tools/production/custom_binaries/..."
    mkdir -p "dist/${RELEASE_NAME}/service_tui/custom_binaries"
    cp "{{ _prod_dir }}/custom_binaries/"*.bin "dist/${RELEASE_NAME}/service_tui/custom_binaries/"

    echo "  ✅  dist/${RELEASE_NAME}"
