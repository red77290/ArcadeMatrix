#pragma once
#include <stdint.h>

// Pixel sprites for the Pac-Man clock face, one uint16_t row mask per line, MSB = leftmost column.
// Pac-Man frames are 13x13; ghosts are 14 wide (12-row body + 2-row skirt). EYES/PUPIL_* overlay the
// body from rows 3 / 5; FRIGHT_FACE (eyes + wavy mouth of a frightened ghost) overlays from row 5.

static const uint8_t PAC_FRAME_CLOSED_ROWS = 13;
static const uint8_t PAC_FRAME_CLOSED_COLS = 13;
static const uint16_t PAC_FRAME_CLOSED[13] = { 0x01F0, 0x07FC, 0x0FFE, 0x0FFE, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x0FFE, 0x0FFE, 0x07FC, 0x01F0 };

static const uint8_t PAC_FRAME_HALF_ROWS = 13;
static const uint8_t PAC_FRAME_HALF_COLS = 13;
static const uint16_t PAC_FRAME_HALF[13] = { 0x01F0, 0x07FC, 0x0FFE, 0x0FFC, 0x1FF8, 0x1FF0, 0x1FC0, 0x1FF0, 0x1FF8, 0x0FFC, 0x0FFE, 0x07FC, 0x01F0 };

static const uint8_t PAC_FRAME_OPEN_ROWS = 13;
static const uint8_t PAC_FRAME_OPEN_COLS = 13;
static const uint16_t PAC_FRAME_OPEN[13] = { 0x01F0, 0x07F8, 0x0FF0, 0x0FE0, 0x1FC0, 0x1F80, 0x1F00, 0x1F80, 0x1FC0, 0x0FE0, 0x0FF0, 0x07F8, 0x01F0 };

static const uint8_t GHOST_BODY_ROWS = 12;
static const uint8_t GHOST_BODY_COLS = 14;
static const uint16_t GHOST_BODY[12] = { 0x01E0, 0x07F8, 0x0FFC, 0x1FFE, 0x1FFE, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF };

static const uint8_t SKIRT_A_ROWS = 2;
static const uint8_t SKIRT_A_COLS = 14;
static const uint16_t SKIRT_A[2] = { 0x39E7, 0x30C3 };

static const uint8_t SKIRT_B_ROWS = 2;
static const uint8_t SKIRT_B_COLS = 14;
static const uint16_t SKIRT_B[2] = { 0x2F3D, 0x0618 };

static const uint8_t EYES_ROWS = 5;
static const uint8_t EYES_COLS = 14;
static const uint16_t EYES[5] = { 0x0618, 0x0F3C, 0x0F3C, 0x0F3C, 0x0618 };

static const uint8_t PUPIL_R_ROWS = 2;
static const uint8_t PUPIL_R_COLS = 14;
static const uint16_t PUPIL_R[2] = { 0x030C, 0x030C };

static const uint8_t PUPIL_L_ROWS = 2;
static const uint8_t PUPIL_L_COLS = 14;
static const uint16_t PUPIL_L[2] = { 0x0C30, 0x0C30 };

static const uint8_t FRIGHT_FACE_ROWS = 5;
static const uint8_t FRIGHT_FACE_COLS = 14;
static const uint16_t FRIGHT_FACE[5] = { 0x0618, 0x0618, 0x0000, 0x1332, 0x2CCD };

