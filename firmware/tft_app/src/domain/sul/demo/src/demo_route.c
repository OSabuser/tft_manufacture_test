#include "demo_route.h"

/* Скриптованный маршрут (согласовано с пользователем, Фаза 3.3): этаж 1 -> 11
 * с промежуточной остановкой на 7 (едет вверх), затем 11 -> 1 с промежуточной
 * остановкой на 3 (едет вниз), повтор. Остановки/концы — короткий гонг
 * (arrival). mode = SUL_MODE_NORMAL везде — спецрежим (перегруз, пожар, ...)
 * на конкретном шаге задаётся этим полем, см. demo_step_t в demo_route.h. */
const demo_step_t K_DEMO_ROUTE[DEMO_ROUTE_LEN] = {
    { "1", 1U, SUL_DIR_NONE, false, SUL_MODE_NORMAL },
    { "1", 1U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "2", 2U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "3", 3U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "4", 4U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "5", 5U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "6", 6U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "7", 7U, SUL_DIR_NONE, true, SUL_MODE_NORMAL }, /* промежуточная остановка вверх */
    { "7", 7U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "8", 8U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "9", 9U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "10", 10U, SUL_DIR_UP, false, SUL_MODE_NORMAL },
    { "11", 11U, SUL_DIR_NONE, true, SUL_MODE_NORMAL }, /* верхний этаж */
    { "11", 11U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "10", 10U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "9", 9U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "8", 8U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "7", 7U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "6", 6U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "5", 5U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "4", 4U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "3", 3U, SUL_DIR_NONE, true, SUL_MODE_NORMAL }, /* промежуточная остановка вниз */
    { "3", 3U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "2", 2U, SUL_DIR_DOWN, false, SUL_MODE_NORMAL },
    { "1", 1U, SUL_DIR_NONE, true, SUL_MODE_NORMAL }, /* нижний этаж, затем цикл сначала */
};
