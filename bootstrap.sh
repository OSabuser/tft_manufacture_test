#!/usr/bin/env bash
# =============================================================================
# bootstrap.sh
# Уровень 0: устанавливает just (если отсутствует), затем передаёт управление
# just host::bootstrap для полной инициализации окружения.
#
# Поддерживаемые платформы:
#   Linux   — bash (native)
#   macOS   — bash (native)
#   Windows — Git Bash (поставляется вместе с git)
#
# Требования: bash >= 4, curl (Linux/macOS) или winget (Windows)
# Запуск: ./bootstrap.sh
# =============================================================================
set -euo pipefail

JUST_VERSION="1.36.0"
LINUX_INSTALL_DIR="${HOME}/.local/bin"

BOLD="\033[1m"
RED="\033[0;31m"
YELLOW="\033[1;33m"
GREEN="\033[0;32m"
RESET="\033[0m"

info()    { echo -e " ${BOLD}${*}${RESET}"; }
success() { echo -e " ${GREEN}✅ ${*}${RESET}"; }
warn()    { echo -e " ${YELLOW}⚠️  ${*}${RESET}"; }
error()   { echo -e " ${RED}❌ ${*}${RESET}"; }

echo ""
echo -e "${BOLD}=== Project Bootstrap ===${RESET}"
echo ""

# -----------------------------------------------------------------------------
# 1. Определить платформу
# -----------------------------------------------------------------------------
_uname="$(uname -s)"
case "${_uname}" in
    Linux*)               PLATFORM="linux"   ;;
    Darwin*)              PLATFORM="macos"   ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    *)
        error "Unsupported platform: ${_uname}"
        exit 1
        ;;
esac

info "Platform detected: ${PLATFORM}"
echo ""

# -----------------------------------------------------------------------------
# 2. Сравнение semver без sort -V (недоступен в Git Bash)
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# 3. Платформо-зависимая установка just
# -----------------------------------------------------------------------------
install_just_linux() {
    info "Installing just ${JUST_VERSION} -> ${LINUX_INSTALL_DIR}"
    mkdir -p "${LINUX_INSTALL_DIR}"
    curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh \
        | bash -s -- --tag "${JUST_VERSION}" --to "${LINUX_INSTALL_DIR}"
    
    if ! echo "${PATH}" | grep -q "${LINUX_INSTALL_DIR}"; then
        warn "${LINUX_INSTALL_DIR} not in PATH -- adding for this session"
        warn "Add to ~/.bashrc to make permanent:"
        warn "  export PATH=\"${LINUX_INSTALL_DIR}:\$PATH\""
        export PATH="${LINUX_INSTALL_DIR}:${PATH}"
    fi
}

install_just_macos() {
    if command -v brew &>/dev/null; then
        info "Installing just via Homebrew..."
        brew install just
    else
        install_just_linux
    fi
}

install_just_windows() {
    if command -v winget &>/dev/null; then
        info "Installing just via winget..."
        winget install --id Casey.Just \
            --accept-package-agreements --accept-source-agreements || {
            error "winget install failed."
            echo "  Run manually in PowerShell: winget install --id Casey.Just"
            echo "  Then restart Git Bash and re-run: ./bootstrap.sh"
            exit 1
        }
        
        warn "Restart Git Bash so the new PATH from winget takes effect,"
        warn "then re-run: ./bootstrap.sh"
        exit 0
    else
        error "winget not found."
        echo ""
        echo "  Options to install just on Windows:"
        echo "  1. Install 'App Installer' from Microsoft Store (brings winget)"
        echo "  2. Download just.exe manually: https://github.com/casey/just/releases"
        echo "     Place it in a directory that is in your Git Bash PATH"
        exit 1
    fi
}

# -----------------------------------------------------------------------------
# 4. Проверить / установить just
# -----------------------------------------------------------------------------
JUST_OK=false

if command -v just &>/dev/null; then
    JUST_CURRENT="$(just --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
    if semver_ge "${JUST_CURRENT}" "${JUST_VERSION}"; then
        success "just ${JUST_CURRENT} (>= ${JUST_VERSION} required)"
        JUST_OK=true
    else
        warn "just ${JUST_CURRENT} is outdated (need >= ${JUST_VERSION}), reinstalling..."
    fi
fi

if [[ "${JUST_OK}" == "false" ]]; then
    case "${PLATFORM}" in
        linux)   install_just_linux   ;;
        macos)   install_just_macos   ;;
        windows) install_just_windows ;;
    esac
    
    JUST_CURRENT="$(just --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
    success "just ${JUST_CURRENT} installed"
    echo ""
fi

# -----------------------------------------------------------------------------
# 5. Передать управление just host::bootstrap для дальнейшей настройки рабочего окружения
# -----------------------------------------------------------------------------
info "Delegating to: just host::bootstrap"
echo ""
exec just host::bootstrap "$@"
