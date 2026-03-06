
#include "app.h"  // Подключение заголовочного файла с тестируемым функционалом
#include "unity.h"

void setUp(void) {
  // вызывется перед каждым тестом
}

void tearDown(void) {
  // после каждого теста
}

void test_add_returns_correct_sum(void) {
  TEST_ASSERT_EQUAL_INT(5, foo_add(2, 3));
}

void test_add_handles_negative(void) {
  TEST_ASSERT_EQUAL_INT(1, foo_add(3, -2));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_add_returns_correct_sum);
  RUN_TEST(test_add_handles_negative);
  return UNITY_END();
}
