idf_component_register(SRCS "powermon.c"
                    PRIV_REQUIRES esp_adc esp_timer esp_driver_gpio
                    INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wstrict-prototypes -Wold-style-definition -Wconversion -Wfloat-equal -Wformat=2 -Wformat-security -Winit-self -Wjump-misses-init -Wlogical-op -Wmissing-include-dirs -Wnested-externs -Wpointer-arith -Wredundant-decls -Wshadow -Wstrict-overflow=5 -Wswitch-default -Wswitch-enum -Wundef -Wunreachable-code -Wunused -Wwrite-strings)
