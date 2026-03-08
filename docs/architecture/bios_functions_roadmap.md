# PSX BIOS Functions Roadmap

This document tracks every known PSX kernel function across the A0, B0, and C0 BIOS
vector tables. Functions that are already implemented (even as minimal stubs) are checked.
Unchecked functions remain to be implemented.

Reference: [PSX-SPX BIOS Function Summary](http://problemkaputt.de/psx-spx.htm#biosfunctionsummary)

Implementation sources: `src/runtime/psx_system_bios_a0.cpp`, `src/runtime/psx_system_bios_b0.cpp`,
`src/runtime/psx_system_bios_c0.cpp`, `src/runtime/psx_system_wait_event.cpp`,
`src/runtime/psx_system_bios_cd.cpp`, `src/runtime/bios_file_table.cpp`

---

## Summary

| Vector  | Implemented | Total known | Coverage |
| ------- | ----------: | ----------: | -------: |
| A0      |          30 |        ~100 |     ~30% |
| B0      |          30 |         ~90 |     ~33% |
| C0      |          11 |         ~30 |     ~37% |
| **All** |      **71** |    **~220** | **~32%** |

---

## A0 Vector Functions (call via `0xA0`)

### File/Device I/O

- [ ] `0x00` — `FileOpen(filename, accessmode)`
- [ ] `0x01` — `FileSeek(fd, offset, seektype)`
- [ ] `0x02` — `FileRead(fd, dst, length)`
- [ ] `0x03` — `FileWrite(fd, src, length)`
- [ ] `0x04` — `FileClose(fd)`
- [ ] `0x05` — `FileIoctl(fd, cmd, arg)`
- [ ] `0x06` — `exit(exitcode)`
- [ ] `0x07` — `FileGetDeviceFlag(fd)`
- [ ] `0x08` — `FileGetc(fd)`
- [ ] `0x09` — `FilePutc(char, fd)`

### TTY / Character I/O

- [ ] `0x0A` — `todigit(char)`
- [ ] `0x0B` — `atof(src)` _(ings)_
- [ ] `0x0C` — `strtoul(src, src_end, base)`
- [ ] `0x0D` — `strtol(src, src_end, base)`
- [ ] `0x0E` — `abs(val)`
- [ ] `0x0F` — `labs(val)`
- [ ] `0x10` — `atoi(src)`
- [ ] `0x11` — `atol(src)`

### String / Memory Utilities

- [ ] `0x12` — `atob(src, num_dst)`
- [x] `0x13` — `setjmp(buf)` — _Stub: stores context, returns 0_
- [ ] `0x14` — `longjmp(buf, param)`
- [ ] `0x15` — `strcat(dst, src)`
- [ ] `0x16` — `strncat(dst, src, maxlen)`
- [x] `0x17` — `strcmp(s1, s2)` — _Functional: byte-by-byte comparison_
- [x] `0x18` — `strncmp(s1, s2, maxlen)` — _Functional: bounded byte-by-byte comparison_
- [x] `0x19` — `strcpy(dst, src)` — _Functional: byte-by-byte copy_
- [ ] `0x1A` — `strncpy(dst, src, maxlen)`
- [x] `0x1B` — `strlen(src)` — _Functional: counts bytes until terminator_
- [ ] `0x1C` — `index(src, char)` / `strchr`
- [ ] `0x1D` — `rindex(src, char)` / `strrchr`
- [ ] `0x1E` — `strchr(src, char)`
- [ ] `0x1F` — `strrchr(src, char)`
- [ ] `0x20` — `strpbrk(src, list)`
- [ ] `0x21` — `strspn(src, list)`
- [ ] `0x22` — `strcspn(src, list)`
- [ ] `0x23` — `strtok(src, list)`
- [ ] `0x24` — `strstr(s1, s2)`

### Memory Operations

- [ ] `0x25` — `toupper(char)`
- [ ] `0x26` — `tolower(char)`
- [x] `0x27` — `bcopy(src, dst, len)` — _Functional: overlap-safe byte copy_
- [x] `0x28` — `bzero(dst, len)` — _Functional: memset to 0_
- [ ] `0x29` — `bcmp(ptr1, ptr2, len)`
- [x] `0x2A` — `memcpy(dst, src, len)` — _Functional: std::memcpy_
- [x] `0x2B` — `memset(dst, char, len)` — _Functional: std::memset_
- [ ] `0x2C` — `memmove(dst, src, len)`
- [ ] `0x2D` — `memcmp(src1, src2, len)`
- [ ] `0x2E` — `memchr(src, char, len)`
- [ ] `0x2F` — `rand()`
- [ ] `0x30` — `srand(seed)`

### Heap

- [ ] `0x31` — `qsort(base, nel, width, callback)`
- [ ] `0x32` — `strtod(x) / _strtod_r()`
- [x] `0x33` — `malloc(size)` — _Stub: bump allocator from top of RAM_
- [x] `0x34` — `free(buf)` — _Stub: no-op_
- [ ] `0x35` — `lsearch(key, base, nel, width, callback)`
- [ ] `0x36` — `bsearch(key, base, nel, width, callback)`
- [ ] `0x37` — `calloc(sizex, sizey)`
- [ ] `0x38` — `realloc(old_buf, new_size)`
- [x] `0x39` — `InitHeap(addr, size)` — _Stub: acknowledged, no-op_

### TTY / Printf

- [ ] `0x3A` — `_exit(exitcode)` / `SystemErrorExit`
- [ ] `0x3B` — `getchar()`
- [x] `0x3C` — `putchar(char)` — _Functional: logs character_
- [ ] `0x3D` — `gets(dst)`
- [x] `0x3E` — `puts(src)` — _Functional: logs string_
- [x] `0x3F` — `printf(txt, param1, ...)` — _Functional: common `%s/%d/%u/%x/%X/%c/%p/%%` logging support_

### Misc System

- [ ] `0x40` — `SystemErrorUnresolvedException()`
- [ ] `0x41` — `LoadExeHeader(filename, headerbuf)`
- [ ] `0x42` — `LoadExeFile(filename, headerbuf)`
- [ ] `0x43` — `DoExecute(headerbuf, param1, param2)`
- [x] `0x44` — `FlushCache()` — _Stub: no-op (host has coherent cache)_
- [ ] `0x45` — `init_a0_b0_c0_vectors()`
- [ ] `0x46` — `GPU_dw(xdst, ydst, xsiz, ysiz, src)`
- [ ] `0x47` — `gpu_send_dma(xdst, ydst, xsiz, ysiz, src)`
- [ ] `0x48` — `SendGP1Command(gp1cmd)`
- [x] `0x49` — `GPU_cw(gp0cmd)` — _Functional: forwards to GPU writeCommand_
- [x] `0x4A` — `GPU_cwp(src, num)` — _Functional: sends GP0 command list_
- [x] `0x4B` — `send_gpu_linked_list(src)` — _Functional: traverses ordering-table linked list and forwards GP0 commands_
- [ ] `0x4C` — `gpu_abort_dma()`
- [ ] `0x4D` — `GetGPUStatus()`
- [x] `0x4E` — `gpu_sync()` — _Functional: reports idle / completes immediately for current runtime model_

### System/Boot (0x50+)

- [ ] `0x51` — `LoadAndExecute(filename, stackbase, stackoffset)`
- [x] `0x54` — `CdInit()` — _Functional: initializes BIOS CD state and issues the Init command_
- [ ] `0x55` — `_bu_init()`
- [x] `0x56` — `CdRemove()` — _Functional: BIOS-facing CD teardown acknowledgment_
- [ ] `0x5B` — `dev_tty_init()`
- [ ] `0x5C` — `dev_tty_open(fd, fcb, "...")`
- [ ] `0x5D` — `dev_tty_action(...)` (in/out/ioctl)
- [ ] `0x5E` — `dev_tty_close()`
- [ ] `0x5F` — `a_]_dummy()`

###Ings / Math / Exception (0x70+)

- [x] `0x70` — `GPU_init()` — _Stub: sends GPU reset + default display mode_
- [x] `0x71` — `_96_init()` _(internal BIOS CD-ROM event init; also mirrored by boot-time setup)_
- [x] `0x72` — `_96_remove()` _(bug-compatible no-op; retail BIOS teardown is not reliable per PSX-SPX)_
- [x] `0x78` — `CdAsyncSeekL(src)` — _Functional: issues `Setloc` + `SeekL` and completes via CD events_
- [x] `0x7C` — `CdAsyncGetStatus(dst)` — _Functional: writes status byte after command completion_
- [x] `0x7E` — `CdAsyncReadSector(count, dst, mode)` — _Functional: schedules BIOS-visible async sector reads_
- [x] `0x81` — `CdAsyncSetMode(mode)` — _Functional: issues `Setmode` and completes via CD events_
- [ ] `0x90` — `CdromIoIrqFunc1()`
- [ ] `0x91` — `CdromDmaIrqFunc1()`
- [ ] `0x92` — `CdromIoIrqFunc2()`
- [ ] `0x93` — `CdromDmaIrqFunc2()`
- [ ] `0x94` — `CdromGetInt5errCode(...)`
- [x] `0x95` — `CdInitSubFunc()` — _Functional: initializes BIOS-owned CD event handles_
- [ ] `0x96` — `AddCDROMDevice()`
- [ ] `0x97` — `AddMemCardDevice()`
- [ ] `0x98` — `AddDuartTtyDevice()`
- [ ] `0x99` — `AddDummyTtyDevice()`
- [ ] `0x9C` — `SetConf(num_ev, num_tcb, stacktop)`
- [ ] `0x9D` — `GetConf(num_ev_dst, num_tcb_dst, stacktop_dst)`
- [ ] `0x9F` — `SetMem(megabytes)`
- [ ] `0xA0` — `_boot()` _(internal)_
- [ ] `0xA1` — `SystemError` / `SystemErrorBootOrDiskFailure`
- [ ] `0xA2` — `EnqueueCdIntr()` _(internal)_
- [ ] `0xA3` — `DequeueCdIntr()` _(internal)_

---

## B0 Vector Functions (call via `0xB0`)

### Kernel Memory

- [x] `0x00` — `alloc_kernel_memory(size)` — _Stub: linear bump allocator_
- [ ] `0x01` — `free_kernel_memory(buf)`

###Ings / Timer

- [ ] `0x02` — `init_timer(t, reload, flags)`
- [ ] `0x03` — `get_timer(t)`
- [ ] `0x04` — `enable_timer_irq(t)`
- [ ] `0x05` — `disable_timer_irq(t)`
- [ ] `0x06` — `restart_timer(t)`

### Events

- [x] `0x07` — `DeliverEvent(class, spec)` — _Functional: delivers matching kernel events and invokes callbacks_
- [x] `0x08` — `OpenEvent(class, spec, mode, func)` — _Functional: allocates real kernel event handles_
- [x] `0x09` — `CloseEvent(event)` — _Functional: closes kernel event handles_
- [x] `0x0A` — `WaitEvent(event)` — _Functional: blocks for `NoCallback` events while pumping runtime hardware_
- [x] `0x0B` — `TestEvent(event)` — _Functional: reports and clears delivered event state_
- [x] `0x0C` — `EnableEvent(event)` — _Functional: enables an existing kernel event_
- [x] `0x0D` — `DisableEvent(event)` — _Functional: disables an existing kernel event_

### Thread Control Block

- [ ] `0x0E` — `OpenThread(pc, sp, gp)`
- [ ] `0x0F` — `CloseThread(handle)`
- [ ] `0x10` — `ChangeThread(handle)`
- [ ] `0x11` — `jump_to_00000000h()` _(crashes)_

### Pad / Controller

- [x] `0x12` — `InitPad(buf1, siz1, buf2, siz2)` — _Stub: no-op_
- [x] `0x13` — `StartPad()` — _Stub: no-op_
- [ ] `0x14` — `StopPad()`
- [ ] `0x15` — `PAD_init(type, button_dest, ...)`
- [ ] `0x16` — `PAD_dr()`

### Exception / Return

- [x] `0x17` — `ReturnFromException()` — _Control-flow signal in callback/IRQ context; COP0 `rfe` handled by IRQ service epilogue_
- [x] `0x18` — `ResetEntryInt()` — _Functional: returns and clears the `HookEntryInt` descriptor_
- [x] `0x19` — `HookEntryInt(addr)` — _Functional: installs the `HookEntryInt` descriptor_

### Misc Kernel

- [ ] `0x1A` — _(unused)_
- [ ] `0x1B` — _(unused)_
- [ ] `0x1C` — _(unused)_
- [ ] `0x1D` — _(unused)_
- [ ] `0x1E` — _(unused)_
- [ ] `0x1F` — _(unused)_
- [x] `0x20` — `UnDeliverEvent(class, spec)` — _Functional: clears delivered state for matching events_

###Ings / String / Memory (B0)

- [ ] `0x25` — _(unused)_
- [ ] `0x2F` — _(unused)_
- [ ] `0x30` — _(unused)_
- [ ] `0x31` — _(unused)_

### File I/O (B0)

- [x] `0x32` — `FileOpen(filename, accessmode)` — _Functional: read-only ISO 9660-backed open over mounted disc contents_
- [x] `0x33` — `FileSeek(fd, offset, seektype)` — _Functional: seek within read-only BIOS file descriptors_
- [x] `0x34` — `FileRead(fd, dst, length)` — _Functional: reads bytes from mounted-disc file extents_
- [ ] `0x35` — `FileWrite(fd, src, length)`
- [x] `0x36` — `FileClose(fd)` — _Functional: closes BIOS file descriptors_
- [ ] `0x37` — `FileIoctl(fd, cmd, arg)`
- [ ] `0x38` — `exit(exitcode)`
- [ ] `0x39` — `FileGetDeviceFlag(fd)`
- [ ] `0x3A` — `FileGetc(fd)`
- [ ] `0x3B` — `FilePutc(char, fd)`

### TTY / Printf (B0)

- [ ] `0x3C` — `std_in_getchar()`
- [x] `0x3D` — `std_out_putchar(char)` — _Functional: logs character_
- [ ] `0x3E` — `std_in_gets(dst)`
- [x] `0x3F` — `std_out_puts(src)` — _Functional: logs string_

### Device Management

- [ ] `0x40` — `chdir(name)`
- [ ] `0x41` — `FormatDevice(devicename)`
- [x] `0x42` — `firstfile(filename, direntry)` — _Functional: enumerates ISO 9660 directory entries into BIOS `DirEntry` structs_
- [x] `0x43` — `nextfile(direntry)` — _Functional: continues BIOS directory enumeration_
- [ ] `0x44` — `FileRename(old_filename, new_filename)`
- [ ] `0x45` — `FileDelete(filename)`
- [x] `0x46` — `undelete(filename)` — _Stub: acknowledged but not implemented_
- [x] `0x47` — `AddDevice(device_info)` — _Stub: returns 1_
- [ ] `0x48` — `RemoveDevice(device_name)`
- [ ] `0x49` — `PrintInstalledDevices()`

### Memory Card / Init

- [x] `0x4A` — `InitCard(pad_enable)` — _Stub: no-op_
- [x] `0x4B` — `StartCard()` — _Stub: no-op_
- [ ] `0x4C` — `StopCard()`
- [ ] `0x4D` — `_card_info_subfunc(port)` _(internal)_
- [ ] `0x4E` — `write_card_sector(port, sector, src)`
- [ ] `0x4F` — `read_card_sector(port, sector, dst)`
- [ ] `0x50` — `allow_new_card()`
- [ ] `0x51` — `Krom2RawAdd(shiftjis_code)`
- [ ] `0x52` — _(unused)_
- [ ] `0x53` — _(unused)_
- [ ] `0x54` — `_get_errno()`
- [ ] `0x55` — `_get_error(fd)`

### BIOS Table Access

- [x] `0x56` — `GetC0Table()` — _Functional: returns the runtime C0 table address_
- [x] `0x57` — `GetB0Table()` — _Functional: returns the runtime B0 table address_
- [ ] `0x58` — `_card_chan()` _(internal)_
- [ ] `0x59` — _(unused)_
- [ ] `0x5A` — _(unused)_
- [x] `0x5B` — `ChangeClearPAD(int)` — _Stub: no-op_

---

## C0 Vector Functions (call via `0xC0`)

### System Initialization

- [x] `0x00` — `EnqueueTimerAndVblankIrqs(priority)` — _Stub: no-op_
- [x] `0x01` — `EnqueueSyscallHandler(priority)` — _Stub: no-op_
- [x] `0x02` — `SysEnqIntRP(priority, struc)` — _Stub: no-op_
- [x] `0x03` — `SysDeqIntRP(priority, struc)` — _Stub: no-op_
- [ ] `0x04` — `get_free_EvCB_slot()`
- [ ] `0x05` — `get_free_TCB_slot()`
- [ ] `0x06` — `ExceptionHandler()` _(internal)_
- [x] `0x07` — `InstallExceptionHandlers()` — _Stub: no-op_
- [x] `0x08` — `SysInitMemory(addr, size)` — _Stub: no-op_
- [x] `0x09` — `SysInitKernelVariables()` — _Stub: no-op_
- [x] `0x0A` — `ChangeClearRCnt(t, flag)` — _Stub: no-op_
- [ ] `0x0B` — `SystemError` / `InitDefInt(3)`
- [x] `0x0C` — `InitDefInt(priority)` — _Stub: no-op_

###Ings / System Setup (C0)

- [ ] `0x0D` — `SetIrqAutoAck(irq, value)`
- [ ] `0x0E` — _(unused)_
- [ ] `0x0F` — _(unused)_
- [ ] `0x10` — _(unused)_
- [ ] `0x11` — _(unused)_
- [x] `0x12` — `InstallDevices(ings)` — _Stub: no-op_
- [ ] `0x13` — `FlushStdInOutPut()`
- [ ] `0x14` — _(unused)_
- [ ] `0x15` — `_cdevinput(circ, char)`
- [ ] `0x16` — `_cdevscan()`
- [ ] `0x17` — `_circgetc(circ)` _(ings)_
- [ ] `0x18` — `_circputc(char, circ)` _(ings)_
- [ ] `0x19` — `ioabort(txt1, txt2)`
- [ ] `0x1A` — `set_card_find_mode(mode)`
- [ ] `0x1B` — `KernelRedirect(ttyflag)`
- [x] `0x1C` — `AdjustA0Table()` — _Stub: no-op_

---

## Priority Guide for Future Implementation

### Tier 1 — Required for most psn00bsdk demos

These functions are commonly called during boot or simple demos and should be
implemented early.

| Vector | ID     | Function               | Status |
| ------ | ------ | ---------------------- | ------ |
| A0     | `0x3F` | `printf`               | ✅     |
| A0     | `0x1B` | `strlen`               | ✅     |
| A0     | `0x2C` | `memmove`              | ❌     |
| A0     | `0x48` | `SendGP1Command`       | ❌     |
| A0     | `0x4B` | `send_gpu_linked_list` | ✅     |
| A0     | `0x4D` | `GetGPUStatus`         | ❌     |
| A0     | `0x4E` | `gpu_sync`             | ❌     |
| B0     | `0x14` | `StopPad`              | ❌     |
| B0     | `0x3C` | `std_in_getchar`       | ❌     |
| C0     | `0x04` | `get_free_EvCB_slot`   | ❌     |
| C0     | `0x05` | `get_free_TCB_slot`    | ❌     |

### Tier 2 — Required for CD-ROM and memory card titles

| Vector | ID     | Function                | Status |
| ------ | ------ | ----------------------- | ------ |
| A0     | `0x54` | `CdInit`                | ✅     |
| A0     | `0x56` | `CdRemove`              | ✅     |
| B0     | `0x32` | `FileOpen` (functional) | ✅     |
| B0     | `0x34` | `FileRead`              | ✅     |
| B0     | `0x36` | `FileClose`             | ✅     |
| B0     | `0x4E` | `write_card_sector`     | ❌     |
| B0     | `0x4F` | `read_card_sector`      | ❌     |

### Tier 3 — Required for multi-threaded / exception-heavy games

| Vector | ID     | Function       | Status |
| ------ | ------ | -------------- | ------ |
| A0     | `0x14` | `longjmp`      | ❌     |
| B0     | `0x0E` | `OpenThread`   | ❌     |
| B0     | `0x0F` | `CloseThread`  | ❌     |
| B0     | `0x10` | `ChangeThread` | ❌     |
| B0     | `0x02` | `init_timer`   | ❌     |
| B0     | `0x03` | `get_timer`    | ❌     |

---

## Notes

- **Stub** implementations acknowledge the call and return a safe default without
  performing real work. They are sufficient to prevent crashes but may cause incorrect
  behavior for games that depend on the return value.
- **Functional** implementations perform real computation (e.g., string operations,
  memory copies) and produce correct results.
- The priority tiers are estimates. Actual requirements depend on which games/demos are
  targeted for testing.
- PSX-SPX documents additional internal and undocumented BIOS calls; these are omitted
  unless encountered in real binaries.

## References

- PSX-SPX BIOS documentation: http://problemkaputt.de/psx-spx.htm#biosfunctionsummary
- Implementation: `src/runtime/psx_system_bios_a0.cpp`, `src/runtime/psx_system_bios_b0.cpp`,
  `src/runtime/psx_system_bios_c0.cpp`, `src/runtime/psx_system_wait_event.cpp`,
  `src/runtime/psx_system_bios_cd.cpp`, `src/runtime/bios_file_table.cpp`
- Runtime library roadmap: `docs/architecture/runtime_library_roadmap.md`
