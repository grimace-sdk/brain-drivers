//------------------------------------------------------------------------------
//
//  File:  framebuffer.h
//
//  Copies the display driver's framebuffer for screenshots.
//
//------------------------------------------------------------------------------
#ifndef __FRAMEBUFFER_H__
#define __FRAMEBUFFER_H__

BOOL FramebufferRead(PBYTE pOut, DWORD OutSize, PDWORD pReturned);

#endif /* __FRAMEBUFFER_H__ */
