/**
  Copyright (c) 2011-2017, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/PrePiLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/TimerLib.h>
#include <Library/HobLib.h>

#include "Sec.h"


#ifndef PERM10_HALT_LEVEL
#define PERM10_HALT_LEVEL 0
#endif

//
// PERM10 bring-up: stop dead at a chosen stage so the device either reboots
// (never reached the stage) or stays black forever (reached it).
//
STATIC
VOID
Perm10Halt (
  IN UINTN  Level
  )
{
  if (Level == PERM10_HALT_LEVEL) {
    for ( ; ; ) {
      __asm__ __volatile__ ("wfi");
    }
  }
}

#ifndef MDEPKG_NDEBUG
#ifndef PERM10_SEC_TRACE
STATIC
VOID
ClearFrameBuffer (VOID)
{
  EFI_MEMORY_REGION_DESCRIPTOR FrameBufferRegion;

  // Locate "Display Reserved" Memory Region
  LocateMemoryRegionByName ("Display Reserved", &FrameBufferRegion);
  LocateMemoryRegionByName ("Display_Reserved", &FrameBufferRegion);

  // Clear Memory Region
  if (FrameBufferRegion.Address != 0 && FrameBufferRegion.Length != 0) {
    ZeroMem ((VOID *)FrameBufferRegion.Address, FrameBufferRegion.Length);
  }
}

#endif

STATIC
VOID
PrintFirmwareVersion (VOID)
{
#ifndef PERM10_SEC_TRACE
  // Clear Frame Buffer
  ClearFrameBuffer ();
#endif

  // Print UEFI Version Message
  DEBUG ((EFI_D_WARN, "\n"));
  DEBUG ((EFI_D_WARN, "Project Silicium for %a %a\n",            FixedPcdGetPtr (PcdSmbiosSystemManufacturer), FixedPcdGetPtr (PcdSmbiosSystemModel)));
  DEBUG ((EFI_D_WARN, "Firmware Version %s Build at %a on %a\n", PcdGetPtr (PcdFirmwareVersionString), __TIME__, __DATE__));
  DEBUG ((EFI_D_WARN, "\n"));
}
#endif

STATIC
EFI_STATUS
InitializeMemory (VOID)
{
  EFI_STATUS                    Status;
  EFI_MEMORY_REGION_DESCRIPTOR  UefiMemoryRegion;
  EFI_HOB_HANDOFF_INFO_TABLE   *HobList;

  // Locate "DXE Heap" Memory Region
  LocateMemoryRegionByName ("DXE Heap", &UefiMemoryRegion);
  LocateMemoryRegionByName ("DXE_Heap", &UefiMemoryRegion);

  // Save UEFI Memory Details
  UINTN UefiMemoryBase = UefiMemoryRegion.Address;
  UINTN UefiMemorySize = UefiMemoryRegion.Length;

  // Verify UEFI Memory Base
  if (UefiMemoryBase == 0 && UefiMemorySize == 0) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate \"DXE Heap\" Memory Region!\n"));
    return EFI_NOT_FOUND;
  }

  // Initialize HOB List
  HobList = HobConstructor ((VOID *)UefiMemoryBase, UefiMemorySize, (VOID *)UefiMemoryBase, (VOID *)(UefiMemoryBase + UefiMemorySize));
  PrePeiSetHobList (HobList);

  // Initialize MMU & Memory HOBs
  Status = MemoryPeim (UefiMemoryBase, UefiMemorySize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Build Stack HOB
  BuildStackHob (FixedPcdGet64 (PcdCPUCoresStackBase), FixedPcdGet32 (PcdCPUCorePrimaryStackSize));

  return EFI_SUCCESS;
}



#ifdef PERM10_SEC_TRACE
//
// Draw a 64-bit value as sixteen hex digits on the splash framebuffer using a
// 5x7 font, so an exception address can be photographed without a serial port.
//
STATIC CONST UINT8  mPerm10Font[16][7] = {
  { 0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F }, // 0
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
  { 0x1F, 0x01, 0x01, 0x1F, 0x10, 0x10, 0x1F }, // 2
  { 0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F }, // 3
  { 0x11, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x01 }, // 4
  { 0x1F, 0x10, 0x10, 0x1F, 0x01, 0x01, 0x1F }, // 5
  { 0x1F, 0x10, 0x10, 0x1F, 0x11, 0x11, 0x1F }, // 6
  { 0x1F, 0x01, 0x01, 0x02, 0x04, 0x04, 0x04 }, // 7
  { 0x1F, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x1F }, // 8
  { 0x1F, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x1F }, // 9
  { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // A
  { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, // B
  { 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F }, // C
  { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, // D
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, // E
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, // F
};

STATIC
VOID
Perm10DrawGlyph (
  IN UINT32  *Pixels,
  IN UINTN    Stride,
  IN UINTN    X,
  IN UINTN    Y,
  IN UINTN    Scale,
  IN UINT32  Colour,
  IN UINT8    Digit
  )
{
  UINTN  Row;
  UINTN  Column;
  UINTN  Dy;
  UINTN  Dx;

  for (Row = 0; Row < 7; Row++) {
    for (Column = 0; Column < 5; Column++) {
      if ((mPerm10Font[Digit][Row] & (0x10 >> Column)) == 0) {
        continue;
      }

      for (Dy = 0; Dy < Scale; Dy++) {
        for (Dx = 0; Dx < Scale; Dx++) {
          Pixels[(Y + Row * Scale + Dy) * Stride + X + Column * Scale + Dx] = Colour;
        }
      }
    }
  }
}

VOID
Perm10PanicPaint (
  IN UINT64  Value
  )
{
  EFI_MEMORY_REGION_DESCRIPTOR  Region;
  UINT32                       *Pixels;
  UINTN                         Count;
  UINTN                         Index;

  // Stop further exceptions while we paint.
  __asm__ __volatile__ ("msr daifset, #0xf");

  Region.Address = 0;
  Region.Length  = 0;
  LocateMemoryRegionByName ("Display Reserved", &Region);
  LocateMemoryRegionByName ("Display_Reserved", &Region);
  if ((Region.Address == 0) || (Region.Length == 0)) {
    for ( ; ; ) {
      __asm__ __volatile__ ("wfi");
    }
  }

  Pixels = (UINT32 *)(UINTN)Region.Address;
  Count  = Region.Length / sizeof (UINT32);
  if (Count > (8u * 1024 * 1024 / sizeof (UINT32))) {
    Count = 8u * 1024 * 1024 / sizeof (UINT32);
  }

  // Solid magenta: "an exception reached the SEC handler".
  for (Index = 0; Index < Count; Index++) {
    Pixels[Index] = 0x00FF00FF;
  }

  (VOID)Value;

  for ( ; ; ) {
    __asm__ __volatile__ ("wfi");
  }
}

#endif

#ifdef PERM10_SEC_TRACE
//
// PERM10 bring-up instrumentation: paint the boot splash framebuffer and hold
// the colour so the last completed initialisation step stays on the panel.
// Colours are documented in Platforms/OPPO/perm10Pkg/README.md.
//
STATIC
VOID
Perm10Trace (
  IN UINT32  Colour
  )
{
  EFI_MEMORY_REGION_DESCRIPTOR  Region;
  UINT32                       *Pixels;
  UINTN                         Count;
  UINTN                         Index;
  volatile UINTN                Spin;

  Region.Address = 0;
  Region.Length  = 0;
  LocateMemoryRegionByName ("Display Reserved", &Region);
  LocateMemoryRegionByName ("Display_Reserved", &Region);
  if ((Region.Address == 0) || (Region.Length == 0)) {
    return;
  }

  Pixels = (UINT32 *)(UINTN)Region.Address;
  Count  = Region.Length / sizeof (UINT32);

  for (Index = 0; Index < Count; Index++) {
    Pixels[Index] = Colour;
  }

  // Roughly a few hundred milliseconds at 1-2 GHz; only needs to be visible.
  for (Spin = 0; Spin < 20000000; Spin++) {
    __asm__ __volatile__ ("nop");
  }
}
#else
#define Perm10Trace(colour)
#endif

STATIC
VOID
SecMain (VOID)
{
  EFI_STATUS Status;

  Perm10Trace (0x0000FFFF); // cyan: entered SecMain

  // Do Platform Specific Initialization
  PlatformInitialize ();
  Perm10Trace (0x00FF0000); // red: PlatformInitialize returned

#ifndef MDEPKG_NDEBUG
  // Initialize Serial Port
  Status = SerialPortInitialize ();
  if (EFI_ERROR (Status)) {
    Perm10Trace (0x00FF00FF); // magenta: serial init failed
    return;
  }

  Perm10Trace (0x00FFFF00); // yellow: serial init done

  // Print Firmware Version
  PrintFirmwareVersion ();

  // Initialize Debug Agent
  InitializeDebugAgent (DEBUG_AGENT_INIT_POSTMEM_SEC, NULL, NULL);
  SaveAndSetDebugTimerInterrupt (TRUE);
#endif

  // Initialize Memory
  Status = InitializeMemory ();
  if (EFI_ERROR (Status)) {
    Perm10Trace (0x00FF0080); // pink: memory init failed
    return;
  }

  Perm10Trace (0x0000FF00); // green: memory init done
  Perm10Halt (2);

  // Build CPU HOB
  BuildCpuHob (ArmGetPhysicalAddressBits (), PcdGet8 (PcdPrePiCpuIoSize));

  // Set the Boot Mode
  SetBootMode (BOOT_WITH_DEFAULT_SETTINGS);

  // Initialize Platform HOBs
  Status = PlatformPeim ();
  if (EFI_ERROR (Status)) {
    Perm10Trace (0x00800040); // purple: PlatformPeim failed
    return;
  }

  Perm10Trace (0x000000FF); // blue: PlatformPeim done
  Perm10Halt (3);

  // Run Library Cconstructors
  ProcessLibraryConstructorList ();

  // Decompress all FVs
  Status = DecompressFirstFv ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Decompress FV! Status = %r\n", Status));
    Perm10Trace (0x00808000); // olive: FV decompression failed
    return;
  }

  Perm10Trace (0x00FFFFFF); // white: FV decompressed
  Perm10Halt (4);

  // Load DXE Core
  Status = LoadDxeCoreFromFv (NULL, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Load DXE Core! Status = %r\n", Status));
  }

#ifdef PERM10_SEC_TRACE
  Perm10Trace (0x00FFFFFF); // white: DXE core queued
#endif
}

VOID
SecEntry (VOID)
{
  Perm10Halt (1);

  // Disable Data Cache
  ArmDisableDataCache ();

  // Invalidate Instruction Cache
  ArmInvalidateInstructionCache ();

  // Enable Instruction Cache
  ArmEnableInstructionCache ();

  // Invalidate Stack & UEFI FD D-Cache
  InvalidateDataCacheRange ((VOID *)FixedPcdGet64 (PcdCPUCoresStackBase), FixedPcdGet32 (PcdCPUCorePrimaryStackSize));
  InvalidateDataCacheRange ((VOID *)FixedPcdGet64 (PcdFdBaseAddress),     FixedPcdGet32 (PcdFdSize));

  // Verify Exception Vector Table
  ASSERT (((UINTN)SecVectorTable & ARM_VECTOR_TABLE_ALIGNMENT) == 0);

  // Enable new Exception Vector Table
  ArmWriteVBar ((UINTN)SecVectorTable);

  // Enter Main SEC Function
  SecMain ();
}
