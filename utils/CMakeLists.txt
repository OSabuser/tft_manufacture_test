# utils/CMakeLists.txt
#
# Платформо-независимые утилиты. Не зависят от SDK, BSP или железа —
# компилируются на хосте и таргете.
#
# Правило включения: код попадает сюда только если выполняются ОБА условия: 1.
# Не зависит от конкретного железа (нет fsl_*, CMSIS, FreeRTOS). 2. Используется
# более чем в одном месте проекта.

add_library(utils STATIC ring_buffer/ring_buffer.c log/log.c
                         prio_queue/prio_queue.c)

# PUBLIC: любой таргет, слинкованный с utils, автоматически получает корень
# utils/ в include path → #include "ring_buffer/ring_buffer.h" #include "log.h"
target_include_directories(utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# LOG_LEVEL пробрасывается из пресета или командной строки. Если не задан —
# log.h выберет сам через NDEBUG.
if(DEFINED LOG_LEVEL)
  target_compile_definitions(utils PUBLIC LOG_LEVEL=${LOG_LEVEL})
endif()
