# Программные компоненты ThirdParty

## Библиотеки сторонних разработчиков, загруженные в виде субмодулей

* [SEGGER_RTT](https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/)
* [Unity](https://www.throwtheswitch.org/unity)
* [FFF](https://github.com/meekrosoft/fff)

## Работа с Git-Submodules

```bash
# 1. Добавление субмодулей (например SEGGER_RTT)
git submodule add https://github.com/SEGGERMicro/RTT ThirdParty/RTT
# 2. Синхронизация состояния субмодулей, загруженных локально с remote
git submodule update --init --recursive

# Полная переинициализация субмодулей (На примере SEGGER_RTT)
# 1. Удалить submodule полностью 
git submodule deinit -f ThirdParty/RTT
rm -rf .git/modules/ThirdParty/RTT
git rm -rf ThirdParty/RTT

# 2. Удалить .gitmodules запись
vim .gitmodules  # удалить секцию [submodule "ThirdParty/RTT"]

# 3. Добавить заново
git submodule add https://github.com/SEGGERMicro/RTT ThirdParty/RTT
git submodule update --init --recursive

```
