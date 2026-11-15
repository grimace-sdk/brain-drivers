//------------------------------------------------------------------------------
//
//  File:  st7586.h
//
//  Sitronix ST7586 controller on SSP2 (SPI2:), 9-bit command/data words.
//
//------------------------------------------------------------------------------
#ifndef __ST7586_H__
#define __ST7586_H__

#define ST7586_COLUMNS      80
#define ST7586_ROWS         120

BOOL St7586Open();
void St7586PowerOn(int VopBase);
void St7586PowerOff();
int St7586CalibratedVopBase();
void St7586SetVopBase(int VopBase);
void St7586SetVopOffset(int Offset);
void St7586StepVop(int Steps);
void St7586WriteWindow(UINT FirstColumn, UINT LastColumn, UINT FirstRow, UINT LastRow, const BYTE* pRows, UINT Stride);

#endif /* __ST7586_H__ */
