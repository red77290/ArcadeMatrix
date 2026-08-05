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

#endif
