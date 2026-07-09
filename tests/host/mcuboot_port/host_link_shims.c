/**
 * @file  host_link_shims.c
 * @brief Заглушки символов, недостающих только при линковке bootutil на
 *        хосте, но не на реальном ARM-таргете. Не часть mcuboot_port/
 *        (портируемого слоя) — специфично для host-тестов.
 *
 * 1. fih_panic_loop() — тело в bootutil/src/fault_injection_hardening.c
 *    написано как ARM inline asm, self-reference по имени без подчёркивания
 *    ("b fih_panic_loop"). На arm-none-eabi-gcc (реальный таргет) это
 *    резолвится нативно — там C-символы не манглятся подчёркиванием.
 *
 *    На хосте картина зависит от ABI, а не просто от "это host-тест":
 *      - Mach-O (macOS): C-функция "fih_panic_loop" компилируется в символ
 *        "_fih_panic_loop" — inline asm ищет ровно "fih_panic_loop" без
 *        подчёркивания и не находит. Нужен явный символ через GNU asm-label
 *        (см. ниже, только под __APPLE__).
 *      - ELF (Linux, напр. devcontainer clang-17): C-символы НЕ манглятся
 *        подчёркиванием — "fih_panic_loop" резолвится сам на себя нативно,
 *        как и на ARM. Наш шим здесь не нужен и создаёт konфликт
 *        ("multiple definition") с уже существующим определением в
 *        fault_injection_hardening.c — поэтому строго под __APPLE__.
 *
 * 2. mbedtls_mpi_read_binary() — используется только mbedtls_asn1_get_mpi()
 *    (RSA-путь ASN.1, ext/mbedtls-asn1/src/asn1parse.c), недостижимо в нашей
 *    ECDSA-only конфигурации. На ARM-таргете --gc-sections вырезает мёртвый
 *    вызов до того, как он потребует символ; host-линковка (Mach-O и ELF
 *    одинаково) без --gc-sections требует явного разрешения — недостижимая
 *    по рантайму заглушка, платформенно-независима.
 */

#if defined(__APPLE__)
void fih_panic_loop_impl(void) asm("fih_panic_loop");
void fih_panic_loop_impl(void)
{
    for (;;)
    {
    }
}
#endif /* __APPLE__ */

int mbedtls_mpi_read_binary(void *X, const unsigned char *buf, unsigned long len)
{
    (void) X;
    (void) buf;
    (void) len;
    return -1; /* недостижимо — ECDSA-only, RSA-путь ASN.1 не вызывается */
}
