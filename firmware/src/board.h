/******************************************************************************
 * board.h  -  Hardware-Schicht: lcd, board_init(), board_set_rotation(),
 *             TFT_BL (Backlight-PWM) und board_touch_read()
 *   Auswahl per Build-Flag: BOARD_JC8012 = GUITION JC8012P4A1C (10,1", 1280x800),
 *   sonst GUITION JC1060P470C (7", 1024x600).
 ******************************************************************************/
#pragma once

#if defined(BOARD_JC8012)
#include "lgfx_jc8012.h"
#else
#include "lgfx_guition.h"
#endif
