//------------------------------------------------------------------------------
//
//  File:  register_dump.h
//
//  Read-only access to a few i.MX28 register blocks, for diagnostics.
//
//------------------------------------------------------------------------------
#ifndef __REGISTER_DUMP_H__
#define __REGISTER_DUMP_H__

DWORD RegisterDump(DWORD Physical, DWORD* pValues, DWORD Count);

#endif /* __REGISTER_DUMP_H__ */
