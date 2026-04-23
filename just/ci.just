# =============================================================================
# ci.just — CI/CD пайплайны
# Выполняется в CI-окружении (GitHub Actions, GitLab CI, Jenkins)
#
# Stage 1: только reproducible шаги без железа:
#   - сборка
#   - host unit-тесты
# lint/release будут включаться по мере стабилизации пайплайна.
# =============================================================================

BUILD_DIR := env('BUILD_DIR', justfile_directory() / 'build')

set working-directory := '..'

[doc('Минимальный CI pipeline: сборка + host-тесты')]
[group('ci')]
pipeline: build test
    @echo "  ✅  Minimal CI pipeline complete"

[doc('Собрать все проекты в Release')]
[group('ci')]
build:
    just build::build-all-release

[doc('Запустить host unit-тесты (Release)')]
[group('ci')]
test:
    just build::test-host-release

[doc('Запустить линтеры и статический анализ')]
[group('ci')]
lint:
    @echo "TODO: enable just build::lint / just build::check after local stabilization"

[doc('Собрать HAB Release-артефакты для выкладки')]
[group('ci')]
release: build
    just build::hab-all-release
    @echo "  ✅  Release artifacts ready in {{ BUILD_DIR }}/Release/"

[group('ci')]
[private]
_coverage:
    #!/usr/bin/env bash
    set -euo pipefail
    if command -v gcovr &>/dev/null; then
        echo "  📊  Generating coverage report..."
        gcovr --xml -o {{ BUILD_DIR }}/coverage.xml
    else
        echo "  ⚠️   gcovr not found, skipping coverage"
    fi
