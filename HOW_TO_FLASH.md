# Прошивка платы со стороны хоста

```bash
# Прошить во Flash
just flash firmware_test debug    # разработка — итерации с отладчиком
just flash firmware_test release  # проверить как будет на сервере
just flash bootloader debug
just flash bootloader release
just flash app debug
just flash app release


# TODO:Загрузить в RAM (без записи во Flash, плата стартует сразу)
just flash-ram firmware_test        # default: debug
just flash-ram firmware_test debug
just flash-ram app release

# TODO:Псевдонимы
just flash-test-debug     # = just flash firmware_test debug
just flash-test-release   # = just flash firmware_test release
just flash-production     # bootloader release + app release
```
