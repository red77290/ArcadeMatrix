#ifndef MSPACSPRITES_H
#define MSPACSPRITES_H

#include <Arduino.h>

/**
 * Ms. Pac-Man, 13x13 to match the Pac-Man frames in PacmanSprites.h.
 *
 * Colour-indexed rather than a plain mask, because she is not one colour: the body is yellow,
 * the bow red with a lighter centre, the eye dark and the lips pink at the mouth.
 *   y body   r bow   p bow highlight   k eye   l lips   . transparent
 */

static const char* const MSPAC_CLOSED[13] = {
    ".rr.rryy.....",
    ".rprpryyyy...",
    ".rryrryyyyy..",
    ".yyyyyykkyyy.",
    ".yyyyyykyyyy.",
    "yyyyyyyyyyyyy",
    "yyyyyyyyyyyyy",
    "yyyyyyyyyyyyy",
    ".yyyyyyyyyyy.",
    ".yyyyyyyyyyy.",
    "..yyyyyyyyy..",
    "...yyyyyyy...",
    ".....yyy.....",
};

static const char* const MSPAC_HALF[13] = {
    ".rr.rryy.....",
    ".rprpryyyy...",
    ".rryrryyyyy..",
    ".yyyyyykkyll.",
    ".yyyyyykll...",
    "yyyyyyyy.....",
    "yyyyyy.......",
    "yyyyyyyy.....",
    ".yyyyyyyyy...",
    ".yyyyyyyyyyy.",
    "..yyyyyyyyy..",
    "...yyyyyyy...",
    ".....yyy.....",
};

static const char* const MSPAC_OPEN[13] = {
    ".rr.rryy.....",
    ".rprpryyyy...",
    ".rryrryyyl...",
    ".yyyyyykk....",
    ".yyyyyyk.....",
    "yyyyyyy......",
    "yyyyyy.......",
    "yyyyyyy......",
    ".yyyyyyy.....",
    ".yyyyyyyy....",
    "..yyyyyyyy...",
    "...yyyyyyy...",
    ".....yyy.....",
};

static const uint8_t MSPAC_ROWS = 13;
static const uint8_t MSPAC_COLS = 13;

#endif
