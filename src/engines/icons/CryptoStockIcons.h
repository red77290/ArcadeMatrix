#pragma once
#include <Arduino.h>

/**
 * @file CryptoStockIcons.h
 * @brief 16x16 / 8x8 pixel-art RGB565 bitmap icons for Cryptocurrencies and Stock Tickers.
 */

// Helper to construct RGB565 color
#define RGB565(r, g, b) (((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3))

// Common Palette Colors
#define C_BITCOIN   RGB565(247, 147, 26)   // Bitcoin Orange
#define C_ETHEREUM  RGB565(98, 126, 234)   // Ethereum Blue-Purple
#define C_SOLANA    RGB565(20, 241, 149)   // Solana Neon Green
#define C_DOGE      RGB565(194, 166, 50)   // Doge Gold
#define C_APPLE     RGB565(225, 225, 225)  // Apple White/Silver
#define C_NVIDIA    RGB565(118, 185, 0)    // Nvidia Green
#define C_TESLA     RGB565(227, 25, 55)    // Tesla Red
#define C_MICROSOFT RGB565(0, 164, 239)   // Microsoft Blue

// 8x8 Mini Icon Bitmaps (RGB565)
static const uint16_t ICON_BTC_8x8[64] = {
    0, C_BITCOIN, C_BITCOIN, C_BITCOIN, C_BITCOIN, C_BITCOIN, 0, 0,
    C_BITCOIN, C_BITCOIN, 0xFFFF, C_BITCOIN, 0xFFFF, C_BITCOIN, C_BITCOIN, 0,
    C_BITCOIN, C_BITCOIN, 0xFFFF, 0xFFFF, C_BITCOIN, C_BITCOIN, C_BITCOIN, 0,
    C_BITCOIN, C_BITCOIN, 0xFFFF, C_BITCOIN, 0xFFFF, C_BITCOIN, C_BITCOIN, 0,
    C_BITCOIN, C_BITCOIN, 0xFFFF, 0xFFFF, C_BITCOIN, C_BITCOIN, C_BITCOIN, 0,
    C_BITCOIN, C_BITCOIN, 0xFFFF, C_BITCOIN, 0xFFFF, C_BITCOIN, C_BITCOIN, 0,
    0, C_BITCOIN, C_BITCOIN, C_BITCOIN, C_BITCOIN, C_BITCOIN, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t ICON_ETH_8x8[64] = {
    0, 0, 0, C_ETHEREUM, C_ETHEREUM, 0, 0, 0,
    0, 0, C_ETHEREUM, 0xFFFF, C_ETHEREUM, 0, 0, 0,
    0, C_ETHEREUM, C_ETHEREUM, 0xFFFF, C_ETHEREUM, C_ETHEREUM, 0, 0,
    C_ETHEREUM, C_ETHEREUM, C_ETHEREUM, 0xFFFF, C_ETHEREUM, C_ETHEREUM, C_ETHEREUM, 0,
    0, C_ETHEREUM, C_ETHEREUM, 0xFFFF, C_ETHEREUM, C_ETHEREUM, 0, 0,
    0, 0, C_ETHEREUM, 0xFFFF, C_ETHEREUM, 0, 0, 0,
    0, 0, 0, C_ETHEREUM, C_ETHEREUM, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t ICON_SOL_8x8[64] = {
    C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, 0, 0, 0,
    0, 0, 0, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA,
    C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, 0, 0, 0,
    0, 0, 0, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA,
    C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, 0, 0, 0,
    0, 0, 0, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA,
    C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, C_SOLANA, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t ICON_AAPL_8x8[64] = {
    0, 0, 0, C_APPLE, 0, 0, 0, 0,
    0, 0, C_APPLE, C_APPLE, C_APPLE, 0, 0, 0,
    0, C_APPLE, C_APPLE, C_APPLE, C_APPLE, C_APPLE, 0, 0,
    0, C_APPLE, C_APPLE, C_APPLE, C_APPLE, C_APPLE, 0, 0,
    0, C_APPLE, C_APPLE, C_APPLE, C_APPLE, C_APPLE, 0, 0,
    0, C_APPLE, C_APPLE, C_APPLE, C_APPLE, C_APPLE, 0, 0,
    0, 0, C_APPLE, 0, C_APPLE, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t ICON_NVDA_8x8[64] = {
    C_NVIDIA, C_NVIDIA, C_NVIDIA, C_NVIDIA, C_NVIDIA, C_NVIDIA, C_NVIDIA, 0,
    C_NVIDIA, 0, 0, 0, 0, 0, C_NVIDIA, 0,
    C_NVIDIA, 0, C_NVIDIA, C_NVIDIA, C_NVIDIA, 0, C_NVIDIA, 0,
    C_NVIDIA, 0, C_NVIDIA, 0xFFFF, C_NVIDIA, 0, C_NVIDIA, 0,
    C_NVIDIA, 0, C_NVIDIA, C_NVIDIA, C_NVIDIA, 0, C_NVIDIA, 0,
    C_NVIDIA, 0, 0, 0, 0, 0, C_NVIDIA, 0,
    C_NVIDIA, C_NVIDIA, C_NVIDIA, C_NVIDIA, C_NVIDIA, C_NVIDIA, C_NVIDIA, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t ICON_TSLA_8x8[64] = {
    C_TESLA, C_TESLA, C_TESLA, C_TESLA, C_TESLA, C_TESLA, C_TESLA, 0,
    0, 0, C_TESLA, C_TESLA, C_TESLA, 0, 0, 0,
    0, 0, 0, C_TESLA, 0, 0, 0, 0,
    0, 0, 0, C_TESLA, 0, 0, 0, 0,
    0, 0, 0, C_TESLA, 0, 0, 0, 0,
    0, 0, 0, C_TESLA, 0, 0, 0, 0,
    0, 0, 0, C_TESLA, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t ICON_MSFT_8x8[64] = {
    0, RGB565(242, 80, 34), RGB565(242, 80, 34), 0, 0, RGB565(127, 186, 0), RGB565(127, 186, 0), 0,
    0, RGB565(242, 80, 34), RGB565(242, 80, 34), 0, 0, RGB565(127, 186, 0), RGB565(127, 186, 0), 0,
    0, RGB565(242, 80, 34), RGB565(242, 80, 34), 0, 0, RGB565(127, 186, 0), RGB565(127, 186, 0), 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, RGB565(0, 164, 239), RGB565(0, 164, 239), 0, 0, RGB565(255, 185, 0), RGB565(255, 185, 0), 0,
    0, RGB565(0, 164, 239), RGB565(0, 164, 239), 0, 0, RGB565(255, 185, 0), RGB565(255, 185, 0), 0,
    0, RGB565(0, 164, 239), RGB565(0, 164, 239), 0, 0, RGB565(255, 185, 0), RGB565(255, 185, 0), 0
};

static const uint16_t ICON_DOGE_8x8[64] = {
    0, C_DOGE, C_DOGE, C_DOGE, C_DOGE, C_DOGE, 0, 0,
    C_DOGE, C_DOGE, 0xFFFF, 0xFFFF, C_DOGE, C_DOGE, C_DOGE, 0,
    C_DOGE, C_DOGE, 0xFFFF, C_DOGE, 0xFFFF, C_DOGE, C_DOGE, 0,
    C_DOGE, C_DOGE, 0xFFFF, C_DOGE, 0xFFFF, C_DOGE, C_DOGE, 0,
    C_DOGE, C_DOGE, 0xFFFF, C_DOGE, 0xFFFF, C_DOGE, C_DOGE, 0,
    C_DOGE, C_DOGE, 0xFFFF, 0xFFFF, C_DOGE, C_DOGE, C_DOGE, 0,
    0, C_DOGE, C_DOGE, C_DOGE, C_DOGE, C_DOGE, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

