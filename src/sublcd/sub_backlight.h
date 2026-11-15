//------------------------------------------------------------------------------
//
//  File:  sub_backlight.h
//
//  Dimmable sub LCD backlight on PWM channel 2.
//
//------------------------------------------------------------------------------
#ifndef __SUB_BACKLIGHT_H__
#define __SUB_BACKLIGHT_H__

void SubBacklightInit();
void SubBacklightSetLevel(DWORD Level);
DWORD SubBacklightGetLevel();

#endif /* __SUB_BACKLIGHT_H__ */
