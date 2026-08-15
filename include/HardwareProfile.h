#pragma once

// Configuration du profil matériel actif
#if defined(HARDWARE_PROFILE_WAVESHARE_S3)

    // Profil : Waveshare ESP32-S3-RGB-Matrix
    #define MATRIX_R1_PIN 4
    #define MATRIX_G1_PIN 5
    #define MATRIX_B1_PIN 6
    #define MATRIX_R2_PIN 7
    #define MATRIX_G2_PIN 15
    #define MATRIX_B2_PIN 16
    #define MATRIX_A_PIN 18
    #define MATRIX_B_PIN 8
    #define MATRIX_C_PIN 3
    #define MATRIX_D_PIN 42
    #define MATRIX_E_PIN 9
    #define MATRIX_LAT_PIN 40
    #define MATRIX_OE_PIN 2
    #define MATRIX_CLK_PIN 41

    // SD Card via bus SD_MMC (1-bit)
    #define USE_SD_MMC 1
    #define SD_MMC_D0_PIN 17
    #define SD_MMC_CMD_PIN 44
    #define SD_MMC_CLK_PIN 1

    // Bus I2C pour Capteur SHTC3 & Codec Audio ES7210
    #ifndef I2C_SDA_PIN
    #define I2C_SDA_PIN 47
    #endif
    #ifndef I2C_SCL_PIN
    #define I2C_SCL_PIN 48
    #endif
    #define SHTC3_I2C_ADDR 0x70
    #define ES7210_I2C_ADDR 0x40

    // Bus I2S pour Audio / Microphone (Cablage reel Waveshare ESP32-S3 RGB Matrix)
    #ifndef I2S_MCLK_PIN
    #define I2S_MCLK_PIN 12
    #endif
    #ifndef I2S_SCLK_PIN
    #define I2S_SCLK_PIN 43
    #endif
    #ifndef I2S_LRCK_PIN
    #define I2S_LRCK_PIN 38
    #endif
    #ifndef I2S_ASDOUT_PIN
    #define I2S_ASDOUT_PIN 39
    #endif

#else

    // Profil par défaut : ESP32 Standard (Retro_Pixel_LED_4_0_0)
    #define MATRIX_R1_PIN 25
    #define MATRIX_G1_PIN 26
    #define MATRIX_B1_PIN 27
    #define MATRIX_R2_PIN 14
    #define MATRIX_G2_PIN 12
    #define MATRIX_B2_PIN 13
    #define MATRIX_A_PIN 33
    #define MATRIX_B_PIN 32
    #define MATRIX_C_PIN 22
    #define MATRIX_D_PIN 17
    #define MATRIX_E_PIN 21
    #define MATRIX_LAT_PIN 4
    #define MATRIX_OE_PIN 15
    #define MATRIX_CLK_PIN 16

    // SD Card via bus VSPI classique
    #define USE_SD_MMC 0
    #define SD_CS_PIN 5
    #define VSPI_SCK 18
    #define VSPI_MISO 19
    #define VSPI_MOSI 23

    // Bus I2C par défaut (ESP32 Standard)
    #ifndef I2C_SDA_PIN
    #define I2C_SDA_PIN 21
    #endif
    #ifndef I2C_SCL_PIN
    #define I2C_SCL_PIN 22
    #endif
    #define SHTC3_I2C_ADDR 0x70

    // Bus I2S par défaut (Microphone I2S standard ex: INMP441)
    #ifndef I2S_SCLK_PIN
    #define I2S_SCLK_PIN 14
    #endif
    #ifndef I2S_LRCK_PIN
    #define I2S_LRCK_PIN 15
    #endif
    #ifndef I2S_ASDOUT_PIN
    #define I2S_ASDOUT_PIN 32
    #endif

#endif
