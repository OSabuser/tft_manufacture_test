# =============================================================================
# ci.just — CI/CD пайплайны
# Выполняется в CI-окружении (GitHub Actions, GitLab CI, Jenkins)
#
# Предполагается что CI работает внутри devcontainer.
# =============================================================================

# === Импорт общих переменных ===
BUILD_DIR := env('BUILD_DIR', justfile_directory() / 'build')
set working-directory := '..'
# =============================================================================
# ГРУППА: ci — полные пайплайны
# =============================================================================

# Полный CI pipeline: сборка + тесты + линтеры
[group('ci')]
pipeline: build test lint
    @echo "  ✅  CI pipeline complete"

# Сборка всех проектов в Release
[group('ci')]
build:
    just build::build-all-release

# Unit-тесты
[group('ci')]
test:
    just build::test-host-release

# TODO: Линтеры + статический анализ
[group('ci')]
lint:
    echo "TODO"
#just build::lint
#just build::check

# Генерация HAB-образов для релиза
[group('ci')]
release: build
    just build::hab-all-release
    @echo "  ✅  Release artifacts ready in {{BUILD_DIR}}/Release/"

# Генерация coverage отчёта (если gcov настроен)
[group('ci'), private]
_coverage:
    #!/usr/bin/env bash
    set -euo pipefail
    if command -v gcovr &>/dev/null; then
        echo "  📊  Generating coverage report..."
        gcovr --xml -o {{BUILD_DIR}}/coverage.xml
    else
        echo "  ⚠️   gcovr not found, skipping coverage"
    fi
