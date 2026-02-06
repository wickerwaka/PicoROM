#if !defined(FLASH_NAME_H)
#define FLASH_NAME_H 1

#ifdef __cplusplus
extern "C"
{
#endif

#define CONFIG_NAME_LEN 18
#define CONFIG_ROM_NAME_LEN 32

    const char *flash_get_device_name(void);

#ifdef __cplusplus
}
#endif

#endif // FLASH_NAME_H
