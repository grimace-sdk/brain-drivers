//------------------------------------------------------------------------------
//
//  File:  ssp_dma.h
//
//  DMA transfers of 9-bit SPI words on SSP2.
//
//------------------------------------------------------------------------------
#ifndef __SSP_DMA_H__
#define __SSP_DMA_H__

UINT16* SspDmaInit(DWORD Words);
BOOL SspDmaConfigure();
BOOL SspDmaSend(const UINT16* pWords, DWORD Count);
void SspDmaSetNoYield(BOOL bNoYield);

#endif /* __SSP_DMA_H__ */
