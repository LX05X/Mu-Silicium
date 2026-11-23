/**
  Copyright (c) 2012-2013, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>

#include "Sec.h"

#ifdef PERM10_SEC_TRACE
VOID
Perm10PanicPaint (
  IN UINT64  Value
  );
#endif

VOID
SecCommonExceptionEntry (
  IN UINT32 Type,
  IN UINTN  LR)
{
  CHAR8 Buffer[100];
  UINTN CharCount;

#ifdef PERM10_SEC_TRACE
  Perm10PanicPaint ((UINT64)LR);
  for ( ; ; ) {
    __asm__ __volatile__ ("wfi");
  }
#endif

  // Set Exception Message
  switch (Type) {
    case EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "Synchronous Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_IRQ:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "IRQ Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_FIQ:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "FIQ Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_SERROR:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "SError/Abort Exception at 0x%llx\n", LR);
      break;

    default:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "Unknown Exception at 0x%llx\n", LR);
      break;
  }

  // Show Exception Message
  SerialPortWrite ((UINT8 *)Buffer, CharCount);

  // CPU Dead Loop
  CpuDeadLoop ();
}
