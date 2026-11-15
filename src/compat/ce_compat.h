#ifndef __CE_COMPAT_H__
#define __CE_COMPAT_H__

#include <pkfuncs.h>

#define SH_WMGR                     17

DWORD WaitForAPIReady(DWORD dwAPISlot, DWORD dwTimeout);

#define CACHE_SYNC_WRITEBACK        0x004

void CacheSync(int iFlags);

#undef RETAILMSG
#define RETAILMSG(Condition, Arguments) ((Condition) ? (NKDbgPrintfW Arguments, 1) : 0)

#endif /* __CE_COMPAT_H__ */
