//------------------------------------------------------------------------------
//
//  File:  pin_sampler.h
//
//  Counts edges on the i.MX28 GPIO inputs.
//
//------------------------------------------------------------------------------
#ifndef __PIN_SAMPLER_H__
#define __PIN_SAMPLER_H__

#include "board_ioctl.h"

BOOL PinSample(DWORD Milliseconds, BRD_PIN_SAMPLE* pResult);

#endif /* __PIN_SAMPLER_H__ */
