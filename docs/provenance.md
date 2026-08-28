# Hardware and platform provenance

The project uses primary sources as technical documentation. It does not copy a Linux
driver implementation into the Windows driver.

## E-MU / EMU10K2 and Hana

- Linux EMU10K1/EMU10K2 card table and E-MU initialization:
  <https://github.com/torvalds/linux/blob/master/sound/pci/emu10k1/emu10k1_main.c>
- Public register and Hana interface definitions:
  <https://github.com/torvalds/linux/blob/master/include/sound/emu10k1.h>
- FPGA access and clock observation behavior:
  <https://github.com/torvalds/linux/blob/master/sound/pci/emu10k1/io.c>
- Capture buffer and PCM period behavior:
  <https://github.com/torvalds/linux/blob/master/sound/pci/emu10k1/emupcm.c>
- Interrupt acknowledgement and capture event classes:
  <https://github.com/torvalds/linux/blob/master/sound/pci/emu10k1/irq.c>

The Linux sources are GPL-2.0-only. Numerical hardware identities and externally
observable interface facts are independently represented here; implementation code is
not transplanted.

## Windows audio driver model

- `IRP_MN_START_DEVICE` raw and translated resources:
  <https://learn.microsoft.com/windows-hardware/drivers/kernel/irp-mn-start-device>
- Starting a device and retaining translated resources:
  <https://learn.microsoft.com/windows-hardware/drivers/kernel/starting-a-device-in-a-function-driver>
- `CM_RESOURCE_LIST` layout:
  <https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/ns-wdm-_cm_resource_list>
- Hardware ID retrieval with `IoGetDeviceProperty`:
  <https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/nf-wdm-iogetdeviceproperty>
- PortCls adapter-driver initialization:
  <https://learn.microsoft.com/windows-hardware/drivers/ddi/portcls/nf-portcls-pcinitializeadapterdriver>

- Microsoft WaveRT design guidance:
  <https://learn.microsoft.com/windows-hardware/drivers/audio/developing-a-wavert-miniport-driver>
- WaveRT miniport interfaces:
  <https://learn.microsoft.com/windows-hardware/drivers/audio/wavert-miniport-driver>
- Microsoft SysVAD reference sample:
  <https://github.com/microsoft/Windows-driver-samples/tree/main/audio/sysvad>

## CPU VST3 worker

- Official Steinberg VST3 SDK:
  <https://github.com/steinbergmedia/vst3sdk>

The VST3 SDK is MIT-licensed. It will be integrated only in the user-mode x64 worker,
never in the kernel or dry-capture dispatch path.
