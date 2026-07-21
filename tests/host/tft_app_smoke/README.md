# test_tft_app_smoke

Фаза 0: проверка работоспособности host-харнесса tft_app.

На Фазе 0 в `firmware/tft_app` ещё нет чистой доменной логики — тест лишь
подтверждает, что таргет собирается и запускается. На Фазе 1 заменяется
реальными доменными тестами (декодер НКУ-CAN → `sul_result_t`, `controller`
diff). См. [firmware/tft_app/PLAN.md](../../../firmware/tft_app/PLAN.md).
