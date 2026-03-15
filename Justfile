# =============================================================================
# Корневой justfile — точка входа для всех команд
# Может выполняться на хосте или внутри devcontainer
#
# Быстрый старт:
#   just             # показать все доступные команды
#   just bootstrap   # первая настройка после git clone (на хосте)
#   just build ...   # сборка (внутри devcontainer)
#   just host::flash # прошивка (на хосте)
# =============================================================================

# === Глобальные настройки ===
# Все модули наследуют эти настройки

# Bash вместо sh; -e прерывает при ошибке, -u — при необъявленной переменной,
# -o pipefail — при ошибке в любом элементе пайпа
set shell := ["bash", "-euo", "pipefail", "-c"]
# экспортируем все just-переменные в окружение
set export
# автоматически загружать .env           
set dotenv-load      

# === Общие переменные (доступны во всех модулях через export) ===
BOARD      := env('BOARD', 'MIMXRT1052')
BUILD_DIR  := env('BUILD_DIR', justfile_directory() / 'build')
TOOLS_DIR  := env('TOOLS_DIR', justfile_directory() / 'tools/host')
CACHE_DIR  := justfile_directory() / '.cache'

# === Модули ===
# Каждый модуль — это namespace с изолированными рецептами
# host.just — операции на хост-машине
mod host 'just/host.just'
# build.just — сборка в DevContainer   
mod build 'just/build.just'
# ci.just — CI/CD сценарии (опционально)  
mod ci 'just/ci.just'   

# === Default рецепт ===
# Вызывается при `just` без аргументов
# Показывает полное дерево команд из всех модулей

default:
    @just --list --list-submodules

# === Популярные алиасы (для удобства команды) ===
# Сокращают длинные вызовы модулей

# Инициализация окружения (эквивалент host::bootstrap)
init:
    @just host::bootstrap

# Быстрая прошивка тестового образа в Debug
flash:
    @just host::flash-test-debug

# Полная сборка всех проектов в Release (в контейнере)
build-all:
    @just build::hab-all-release

# CI пайплайн
run-ci:
    @just ci::pipeline




