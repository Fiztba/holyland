#include "kf_texture.h"

/* a doubleword stored through a union with a char in it, which may alias anything: the table's
   count is read again after each store */
typedef union {
    u64 value;
    char byte; // match: makes the stores alias the table
} AliasedWord;

/* a GS BITBLTBUF value with its destination base pointer (bits 32-45, in 64-word blocks) set to `base` */
#define SET_DESTINATION_BASE(bitbltbuf, base)     ((bitbltbuf) = ((bitbltbuf) & ~((u64)0x3FFF << 32)) | ((s64)((base) & 0x3FFF) << 32))

u32 *func_00109990(int);
void func_00109650(u32 *, u32 *);
void func_00109E18(void);

/* Queue every chunk of a texture table for sending. */
void func_0010B238(TextureTableHeader *table) {
    TextureTableEntry *entries = (TextureTableEntry *)(table + 1);
    u32 *packet_start;
    u32 *packet;
    u32 *chunk;
    u32 i;

    func_00109E18();
    packet_start = packet = func_00109990(table->entry_count + 1);
    for (i = 0; i < table->entry_count; i++) {
        chunk = (u32 *)((char *)table + entries[i].offset);
        *packet++ = (*chunk & 0x8FFFFFFF) | 0x30000000;
        *packet++ = (u32)(chunk + 4);
        *packet++ = 0;
        *packet++ = 0;
    }
    func_00109650(packet_start, packet);
}

/* Queue the chunks of a texture table that are neither switched off nor in skip_mask. */
void func_0010B2E0(TextureTableHeader *table_arg, s32 skip_mask) {
    TextureTableHeader *table = table_arg; // match: a copy of the parameter keeps the count reload at the loop end
    TextureTableEntry *entries;
    u32 *packet_start = NULL;
    u32 *packet;
    u32 *chunk;
    s32 i;

    func_00109E18();
    entries = (TextureTableEntry *)(table + 1);
    for (i = 0; i < table->entry_count; i++) {
        if (entries[i].unk0C == 0 && (i >= 32 || !((skip_mask >> i) & 1))) {
            packet = func_00109990(1);
            if (packet_start == NULL) {
                packet_start = packet;
            }
            chunk = (u32 *)((char *)table + entries[i].offset);
            *packet++ = (*chunk & 0x8FFFFFFF) | 0x30000000;
            *packet++ = (u32)(chunk + 4);
            *packet++ = 0;
            *packet++ = 0;
        }
    }
    if (packet_start != NULL) {
        func_00109650(packet_start, func_00109990(1));
    }
}

/* Queue one chunk of a texture table for sending. */
void func_0010B410(TextureTableHeader *table, s32 index) {
    TextureTableEntry *entries = (TextureTableEntry *)(table + 1);
    u32 *packet_start;
    u32 *packet;
    u32 *chunk;

    if (index >= 0 && index < table->entry_count) {
        func_00109E18();
        packet_start = packet = func_00109990(2);
        chunk = (u32 *)((char *)table + entries[index].offset);
        *packet++ = (*chunk & 0x8FFFFFFF) | 0x30000000;
        *packet++ = (u32)(chunk + 4);
        *packet++ = 0;
        *packet++ = 0;
        func_00109650(packet_start, packet);
    }
}

/* How many chunks a texture table has. */
int func_0010B4D8(TextureTableHeader *table) {
    return table->entry_count;
}

/* Switch one chunk of a texture table on or off for func_0010B2E0. */
void func_0010B4E0(TextureTableHeader *table, s32 index, s32 enable) {
    TextureTableEntry *entries = (TextureTableEntry *)(table + 1);

    if (index >= 0 && index < table->entry_count) {
        entries[index].unk0C = enable == 0;
    }
}

/* Moves the textures of a table in GS memory: each chunk's image upload goes to texture_base and,
   when the image is not the chunk's last upload, the CLUT upload after it to clut_base (0: left as
   it is). */
void func_0010B510(TextureTableHeader *table, int texture_base, int clut_base) {
    TextureTableEntry *entries;
    u32 i;

    entries = (TextureTableEntry *)(table + 1);
    i = 0;
    if (table->entry_count != 0) do {
        char *cursor = (char *)table + entries[i].offset;
        u64 image_tag;

        cursor += 0x40; /* the image upload's BITBLTBUF */
        if (texture_base != 0) {
            SET_DESTINATION_BASE(((AliasedWord *)cursor)->value, texture_base);
        }
        image_tag = ((AliasedWord *)cursor)[8].value; /* the GIF tag of the image data */
        cursor += 0x50;
        if ((int)((image_tag >> 15) & 1) == 0) { /* not the end of the packet: a CLUT upload follows */
            cursor += (int)(image_tag & 0x7FFF) * 16;
            cursor += 0x20;
            if (clut_base != 0) {
                SET_DESTINATION_BASE(((AliasedWord *)cursor)->value, clut_base);
            }
        }
        i++;
    } while (i < table->entry_count);
}
