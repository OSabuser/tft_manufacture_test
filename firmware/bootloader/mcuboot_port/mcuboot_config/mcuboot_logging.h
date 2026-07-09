/**
 * @file  mcuboot_logging.h
 * @brief Логирование bootutil — не используется (MCUBOOT_HAVE_LOGGING не
 *        определён в mcuboot_config.h, BOOT_LOG_* становятся no-op через
 *        bootutil_log.h). Этот заголовок обязателен к существованию —
 *        часть заголовков bootutil (bootutil/crypto/sha.h) включает его
 *        безусловно, независимо от MCUBOOT_HAVE_LOGGING.
 */

#ifndef MCUBOOT_LOGGING_H_
#define MCUBOOT_LOGGING_H_

#define MCUBOOT_LOG_MODULE_DECLARE(domain)
#define MCUBOOT_LOG_MODULE_REGISTER(domain)

#define MCUBOOT_LOG_ERR(...)
#define MCUBOOT_LOG_WRN(...)
#define MCUBOOT_LOG_INF(...)
#define MCUBOOT_LOG_DBG(...)

#endif /* MCUBOOT_LOGGING_H_ */
