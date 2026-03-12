# =============================================================================
# host.just — операции на хост-машине (вне devcontainer)
#
# Ответственность:
#   - bootstrap окружения (just, uv, docker, udev)
#   - USB-прошивка через spsdk
#   - HIL-тесты на реальном железе
#   - производственные сценарии (incoming, production)
#
# Все рецепты должны работать БЕЗ devcontainer.
# =============================================================================

# === Импорт общих переменных из корневого justfile ===
# Благодаря `set export` в корне, эти переменные доступны
TOOLS_DIR := env('TOOLS_DIR', justfile_directory() / 'tools/host')
CACHE_DIR := env('CACHE_DIR', justfile_directory() / '.cache')

# === Минимальные версии зависимостей ===
JUST_MIN   := "1.36.0"
UV_MIN     := "0.4.0"
DOCKER_MIN := "24.0.0"
set working-directory := '..'
# =============================================================================
# ГРУППА: setup — инициализация рабочего окружения
# =============================================================================

# [1/3] Проверить наличие и версии инструментов на хост-машине
[group('setup')]
check-deps:
    #!/usr/bin/env bash
    set -euo pipefail

    BOLD="\033[1m"; GREEN="\033[0;32m"; RED="\033[0;31m"; YELLOW="\033[1;33m"; RESET="\033[0m"
    ERRORS=0; WARNINGS=0

    ok()   { echo -e "  ${GREEN}✅  ${1}${RESET}"; }
    fail() { echo -e "  ${RED}❌  ${1}${RESET}"; ERRORS=$((ERRORS+1)); }
    warn() { echo -e "  ${YELLOW}⚠️   ${1}${RESET}"; WARNINGS=$((WARNINGS+1)); }

    _uname="$(uname -s)"
    case "${_uname}" in
        Linux*)               PLATFORM="linux"   ;;
        Darwin*)              PLATFORM="macos"   ;;
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
        *)                    PLATFORM="unknown" ;;
    esac

    # Сравнение semver без sort -V (недоступен в Git Bash)
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
        case "${PLATFORM}" in
            linux)
                case "$tool" in
                    just)   echo "curl -sSf https://just.systems/install.sh | bash -s -- --to ~/.local/bin" ;;
                    uv)     echo "curl -LsSf https://astral.sh/uv/install.sh | sh" ;;
                    docker) echo "https://docs.docker.com/engine/install/ubuntu/" ;;
                esac ;;
            macos)
                case "$tool" in
                    just)   echo "brew install just" ;;
                    uv)     echo "brew install uv" ;;
                    docker) echo "brew install --cask docker" ;;
                esac ;;
            windows)
                case "$tool" in
                    just)   echo "winget install --id Casey.Just  (в PowerShell)" ;;
                    uv)     echo 'powershell -c "irm https://astral.sh/uv/install.ps1 | iex"' ;;
                    docker) echo "winget install --id Docker.DockerDesktop  (в PowerShell)" ;;
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
    echo -e "${BOLD}  Platform: ${PLATFORM}${RESET}"
    echo ""
    echo -e "${BOLD}  Checking host dependencies...${RESET}"
    echo ""

    check_tool "just"   "just"   "{{JUST_MIN}}"
    check_tool "uv"     "uv"     "{{UV_MIN}}"
    check_tool "docker" "docker" "{{DOCKER_MIN}}" version --format '{{{{.Client.Version}}}}'

    if [[ "${PLATFORM}" == "linux" ]]; then
        echo ""
        echo -e "${BOLD}  Checking udev rules (Linux only)...${RESET}"
        echo ""
        rules_file="/etc/udev/rules.d/99-nxp-mimxrt.rules"
        udev_ok=true
        grep -q "1fc9" "$rules_file" 2>/dev/null || { warn "udev: BootROM rule missing (VID 1fc9)"; udev_ok=false; }
        grep -q "15a2" "$rules_file" 2>/dev/null || { warn "udev: Flashloader rule missing (VID 15a2)"; udev_ok=false; }
        if ! groups 2>/dev/null | grep -q plugdev; then
            warn "User '$(whoami)' not in 'plugdev' group"
            udev_ok=false
        fi
        if [[ "$udev_ok" == "false" ]]; then
            warn "USB flashing requires root without udev rules -- run: just setup-udev"
        else
            ok "udev rules (NXP USB) + plugdev group"
        fi
    fi

    echo ""
    if [[ $ERRORS -gt 0 ]]; then
        echo -e "  ${RED}${BOLD}$ERRORS error(s) found. Fix them before continuing.${RESET}"
        echo ""
        exit 1
    elif [[ $WARNINGS -gt 0 ]]; then
        echo -e "  ${YELLOW}${BOLD}$WARNINGS warning(s). Some features may not work.${RESET}"
    else
        echo -e "  ${GREEN}${BOLD}All dependencies satisfied.${RESET}"
    fi
    echo ""

# [2/3] Установить udev правила для USB-прошивки (только Linux, требует sudo)
[group('setup')]
setup-udev:
    #!/usr/bin/env bash
    set -euo pipefail
    case "$(uname -s)" in
        Linux*) ;;
        *)
            echo "  ℹ️  udev rules are Linux-only, skipping on $(uname -s)."
            exit 0
            ;;
    esac
    echo "  Installing udev rules for NXP USB devices..."
    printf '%s\n%s\n%s\n%s\n' \
        '# NXP BootROM -- SDP mode (BOOT_MOD_1 = 3V3)' \
        'SUBSYSTEM=="usb", ATTR{idVendor}=="1fc9", ATTR{idProduct}=="0130", MODE="0666", GROUP="plugdev"' \
        '# NXP Flashloader -- after jump-address' \
        'SUBSYSTEM=="usb", ATTR{idVendor}=="15a2", ATTR{idProduct}=="0073", MODE="0666", GROUP="plugdev"' \
        | sudo tee /etc/udev/rules.d/99-nxp-mimxrt.rules > /dev/null
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    sudo usermod -a -G plugdev "$USER"
    echo "  ✅  udev rules installed"
    echo "  ⚠️   Re-login required for 'plugdev' group to take effect"

# [3/3] Синхронизировать зависимости tools/host (spsdk и др.)
# Идемпотентен: пропускает sync если uv.lock не изменился
[group('setup')]
setup-tools:
    #!/usr/bin/env bash
    set -euo pipefail
    LOCK="{{TOOLS_DIR}}/uv.lock"
    STAMP="{{CACHE_DIR}}/uv_lock.sha256"
    mkdir -p "{{CACHE_DIR}}"
    current=$(sha256sum "$LOCK" 2>/dev/null | cut -d' ' -f1 || echo "missing")
    previous=$(cat "$STAMP" 2>/dev/null || echo "none")
    if [[ "$current" == "$previous" ]]; then
        echo "  ✅  tools/host deps up to date (lockfile unchanged)"
    else
        echo "  📦  Syncing tools/host deps..."
        cd "{{TOOLS_DIR}}" && uv sync
        echo "$current" > "$STAMP"
        echo "  ✅  tools/host deps installed"
    fi

# Полная инициализация — запустить один раз после git clone
[group('setup')]
bootstrap:
    #!/usr/bin/env bash
    set -euo pipefail
    _uname="$(uname -s)"
    case "${_uname}" in
        Linux*)               PLATFORM="linux"   ;;
        Darwin*)              PLATFORM="macos"   ;;
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
        *)                    PLATFORM="unknown" ;;
    esac
    echo ""
    echo "  === Bootstrap: step 1/3 -- check-deps ==="
    just host::check-deps
    if [[ "${PLATFORM}" == "linux" ]]; then
        echo "  === Bootstrap: step 2/3 -- setup-udev ==="
        just host::setup-udev
    else
        echo "  ℹ️  step 2/3 -- setup-udev skipped (${PLATFORM})"
    fi
    echo "  === Bootstrap: step 3/3 -- setup-tools ==="
    just host::setup-tools
    echo ""
    echo "  ✅  Bootstrap complete."
    echo "  Next: open project in VSCode -> 'Reopen in Container'"
    echo ""

# =============================================================================
# ГРУППА: flash — прошивка платы через USB
# Требует: uv + spsdk (just setup-tools), плата в SDP-режиме
# HAB-образы должны быть собраны заранее (just hab-* внутри devcontainer)
#
# Матрица прошивки:
#   just flash firmware_test debug
#   just flash firmware_test release
#   just flash bootloader    debug
#   just flash bootloader    release
#   just flash app           debug
#   just flash app           release
# =============================================================================

# Прошить выбранный HAB-образ.
# Использование: just flash <project> <type>
#   project: firmware_test | bootloader | app
#   type:    debug | release  (default: release)
#
# Образ должен быть собран заранее в devcontainer:
#   just --justfile just/build.just hab-<project>-<type>
[group('flash')]
flash project type="release":
    #!/usr/bin/env bash
    set -euo pipefail
    case "{{type}}" in
        debug)   BUILD_TYPE="Debug" ;;
        release) BUILD_TYPE="Release" ;;
        *)
            echo "  ❌  Unknown type: {{type}}"
            echo "  Valid: debug, release"
            exit 1 ;;
    esac
    cd "{{TOOLS_DIR}}" && uv run python3 flash_usb.py \
        --firmware "{{project}}" --build-type "${BUILD_TYPE}"

# Загрузить образ в RAM без записи во Flash.
# Полезно для быстрой проверки — не изнашивает Flash, плата стартует сразу.
# Использование: just flash-ram <project> <type>
[group('flash')]
flash-ram project type="debug":
    #!/usr/bin/env bash
    set -euo pipefail
    case "{{type}}" in
        debug)   BUILD_TYPE="Debug" ;;
        release) BUILD_TYPE="Release" ;;
        *)
            echo "  ❌  Unknown type: {{type}}"
            echo "  Valid: debug, release"
            exit 1 ;;
    esac
    cd "{{TOOLS_DIR}}" && uv run python3 flash_usb.py \
        --firmware "{{project}}" --build-type "${BUILD_TYPE}" --ram-only

# Псевдонимы для частых сценариев ────────────────────────────────────────────

# firmware_test Debug — самый частый при разработке
[group('flash')]
flash-test-debug:
    @just host::flash firmware_test debug

# firmware_test Release — как будет на производстве
[group('flash')]
flash-test-release:
    @just host::flash firmware_test release

# TODO: bootloader + app Release — финальная прошивка
[group('flash'), confirm("Flash bootloader + app (Release)?")]
flash-production:
    @just host::flash bootloader release
    @just host::flash app release

# =============================================================================
# ГРУППА: pipeline — производственные сценарии (для сервера)
# =============================================================================

# Входной контроль платы: тестовая прошивка (Release) -> HIL-тесты
[group('pipeline')]
incoming: flash-test-release
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  ▶  Running HIL tests (CAN, UART, SDRAM, SPI Flash)..."
    # cd tools/hil && uv run python3 run_hil.py   <- раскомментить когда готово
    echo "  ⚠️   HIL tests not yet implemented"

# Финальная прошивка: загрузчик + основная прошивка (Release)
[group('pipeline')]
production: flash-production
    @echo "  ✅  Production firmware flashed (bootloader + app)"

# =============================================================================
# ГРУППА: util — вспомогательные инструменты
# =============================================================================

# Найти подключённые NXP USB-устройства
[group('util')]
scan:
    cd "{{TOOLS_DIR}}" && uv run nxpdevscan

# Проверить связь с BootROM через SDP (плата в SDP-режиме)
[group('util'), no-cd]
sdp-status:
    cd "{{TOOLS_DIR}}" && uv run sdphost -u 0x1FC9,0x0130 -- error-status

# Проверить что Flashloader отвечает (после jump-address)
[group('util')]
flashloader-status:
    cd "{{TOOLS_DIR}}" && uv run blhost -u 0x15A2,0x0073 -- get-property 1 0

# Обновить spsdk до новой версии
# Использование: just upgrade-tools 3.8.0
[group('util')]
upgrade-tools version:
    cd "{{TOOLS_DIR}}" && uv add "spsdk=={{version}}" && uv sync
    @echo "  ✅  spsdk upgraded to {{version}}"
    @echo "  Run: git add tools/host/uv.lock tools/host/pyproject.toml"
