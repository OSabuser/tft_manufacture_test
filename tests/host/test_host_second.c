#include "app.h" // Подключение заголовочного файла с тестируемым функционалом
#include "fff.h"
#include "unity.h"
void setUp(void) {
  // Инициализация перед каждым тестом (опционально)
}

void tearDown(void) {
  // Очистка после теста (опционально)
}

// Тест 1: Проверка сложения
void test_app_add_returns_correct_sum(void) {
  TEST_ASSERT_EQUAL_INT(5, foo_add(2, 3));
  TEST_ASSERT_EQUAL_INT(0, foo_add(0, 0));
}

// Тест 2: Отрицательные числа
void test_app_add_handles_negative(void) {
  TEST_ASSERT_EQUAL_INT(-1, foo_add(-2, 1));
}

// Тест 3: Переполнение (boundary case)
void test_app_add_overflow(void) {
  TEST_ASSERT_INT_WITHIN(1, 2147483647, foo_add(2147483646, 1));
}

int main(void) {
  UNITY_BEGIN(); // Запуск Unity
  RUN_TEST(test_app_add_returns_correct_sum);
  RUN_TEST(test_app_add_handles_negative);
  RUN_TEST(test_app_add_overflow);
  return UNITY_END(); // Отчёт
}