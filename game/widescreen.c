/* widescreen.c: PC-only. The game's side of --widescreen (port_widescreen(), port/include/port/host.h).

   The patched game code calls Widescreen_Mark around the 2D it draws through the packet chain (DMA
   channel 1: the HUD, the message box, the pause label), to tell the GS front end
   (port/platform/gs.c) what follows: 2D laid out for 4:3, to squeeze into the 16:9 picture, or the 3D
   world again. The mark is a GS register write of its own, queued in the chain where the call is
   made. With the switch off nothing is queued, and the chain is the PS2's. */
#include "common.h"

void Packet_BeginGsRegisters(void);
void Packet_AddGsRegister(int reg, s64 value);
void Packet_EndGsRegisters(void);

/* what: PORT_WIDESCREEN_. Not to be called between Packet_BeginGsRegisters and Packet_EndGsRegisters. */
void Widescreen_Mark(int what) {
    if (!port_widescreen()) {
        return;
    }
    Packet_BeginGsRegisters();
    Packet_AddGsRegister(PORT_GS_REG_WIDESCREEN, what);
    Packet_EndGsRegisters();
}
