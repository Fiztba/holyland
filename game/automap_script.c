/* automap_script.c: PC-only. The automap mod's map browser (game/automap.c, docs/port/AUTOMAP.md "Browsing other
   maps") shows a map the player is not in as he would find it walking in now. What the game does then, besides
   reading the map's file and the save's tables, is run the map's start script: Map_MakeCurrent calls
   Script_Init and Script_Start(0x7FFF) each time a map becomes the current one, and that script puts the map's
   people in or out of the world by the story's flags (command 253, ScriptCmd_SetEntityFlags: bit 7, whether the
   spawn may come into the world), turns map objects on and off (commands 110 and 111) and puts fixed items in
   place or moves them (100 and 102).

   This runs that script as the interpreter of script/script.c would (its words: labels, "if", "set", jumps,
   commands with their arguments read as Script_ReadArg reads them), on copies: the game's variables
   (script_game_var_blocks) are only read, what the script writes to them goes into a list of the run's own,
   and the threads' and the shared variables are the run's own. Of the commands only those that change or
   read what is shown are carried out, on AutomapScriptWorld; every other command finishes at once, except
   those that never finish in the game (ScriptCmd_Stall, the WaitForever ones, word 0x19D), which end the
   thread there as they hold it there. A thread a script starts (command 2) runs before the one that started
   it goes on when it waits for it, after it when not. Every run is bounded: a script that loops waiting for
   something (a frame count, the player) stops at the bound. */
#include <string.h>

#include "common.h"
#include "automap_script.h"
#include "model/model.h"
#include "script/script.h"

extern u8 *script_game_var_blocks[];
extern u8 item_counts[];
int Game_GetSpawnDeathCount(int map, int entry);

#define MAX_THREADS 24
#define THREAD_WORDS 20000 /* words one thread steps at most */
#define RUN_WORDS 200000   /* and the whole run */
#define MAX_WRITES 512     /* bytes of the game's variables the run writes, at most */
#define MAX_DEPTH 8        /* scripts waited for inside one another */

typedef struct {
    u32 code;   /* the block (low byte) */
    int offset; /* the byte in it */
    u8 value;
} GameVarWrite;

typedef struct {
    AutomapScriptWorld *world;
    u8 shared[0x400];
    GameVarWrite writes[MAX_WRITES];
    int write_count;
    int words; /* stepped so far */
    int stopped; /* command 5: every thread stopped */
    int queue[MAX_THREADS];
    int queue_count;
} Run;

typedef struct {
    u8 locals[0x200];
} Locals;

/* ---- The variables --------------------------------------------------------------------------------- */
static u8 game_byte(Run *run, u32 block, int offset) {
    const u8 *base;
    int k;

    for (k = run->write_count - 1; k >= 0; k--) {
        if (run->writes[k].code == block && run->writes[k].offset == offset) {
            return run->writes[k].value;
        }
    }
    base = block < 0x40 ? script_game_var_blocks[block] : NULL;
    return base != NULL ? base[offset] : 0;
}

/* a value of `size` bytes at `p` (little endian) */
static int load(const u8 *p, int type) {
    switch (type) {
    case 0: return (s8)p[0];
    case 1: return p[0];
    case 2: return (s16)(p[0] | p[1] << 8);
    case 3: return (u16)(p[0] | p[1] << 8);
    default: return (int)((u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24);
    }
}

static int type_size(int type) {
    return type <= 1 ? 1 : type <= 3 ? 2 : 4;
}

/* Script_ReadArg: a constant, or a game, thread or shared variable; kinds 0x11 to 0x13 as they are */
static void read_arg(Run *run, Locals *thread, const u32 *source, ScriptArg *result) {
    u32 word = source[0], code = word >> 8;
    int value = (int)source[1], type = (int)(code >> 8), k;
    u8 bytes[4];

    switch (word & 0xFF) {
    case SCRIPT_ARG_GAME_VAR:
        for (k = 0; k < type_size(type); k++) {
            bytes[k] = game_byte(run, code & 0xFF, value + k);
        }
        result->value = load(bytes, type);
        result->opcode = type;
        break;
    case SCRIPT_ARG_LOCAL_VAR:
        result->value = value >= 0 && value <= 0x1FC ? load(thread->locals + value, type) : 0;
        result->opcode = type;
        break;
    case SCRIPT_ARG_SHARED_VAR:
        result->value = value >= 0 && value <= 0x3FC ? load(run->shared + value, type) : 0;
        result->opcode = type;
        break;
    case SCRIPT_ARG_CONSTANT:
        result->opcode = (int)(word >> 16);
        result->value = value;
        break;
    default:
        result->opcode = (int)word;
        result->value = value;
        break;
    }
}

/* Script_WriteGameVar, Script_WriteLocalVar, Script_WriteSharedVar: into the run's copies */
static void write_var(Run *run, Locals *thread, u32 kind_code, int offset, int value) {
    u32 code = kind_code >> 8;
    int type = (int)(code >> 8), size = type_size(type), k;

    for (k = 0; k < size; k++) {
        u8 byte = (u8)((u32)value >> (8 * k));

        switch (kind_code & 0xF) {
        case SCRIPT_ARG_GAME_VAR:
            if (run->write_count < MAX_WRITES) {
                run->writes[run->write_count].code = code & 0xFF;
                run->writes[run->write_count].offset = offset + k;
                run->writes[run->write_count++].value = byte;
            }
            break;
        case SCRIPT_ARG_LOCAL_VAR:
            if (offset >= 0 && offset + k < 0x200) {
                thread->locals[offset + k] = byte;
            }
            break;
        case SCRIPT_ARG_SHARED_VAR:
            if (offset >= 0 && offset + k < 0x400) {
                run->shared[offset + k] = byte;
            }
            break;
        }
    }
}

/* Script_StoreResult: an answer to the variable an argument of kind 0x11 to 0x13 names */
static void store(Run *run, Locals *thread, const ScriptArg *target, int value) {
    if ((target->opcode & 0xF0) == 0x10) {
        write_var(run, thread, (u32)target->opcode, target->value, value);
    }
}

static float as_float(int bits) {
    float f;

    memcpy(&f, &bits, 4);
    return f;
}

static int float_bits(float f) {
    int bits;

    memcpy(&bits, &f, 4);
    return bits;
}

/* Script_EvalIf: the words to skip when it fails, else 0; *cursor past it */
static int eval_if(Run *run, Locals *thread, const u32 **cursor) {
    const u32 *c = *cursor;
    int count = ((const u8 *)c)[1], join = ((const u8 *)c)[0], skip, i, result = 0;

    c++;
    skip = (int)*c++;
    for (i = 0; i < count; i++) {
        ScriptArg left, right;
        u32 comparison;
        int r = 0;

        read_arg(run, thread, c, &left);
        c += 2;
        comparison = *c++;
        read_arg(run, thread, c, &right);
        c += 2;
        if (left.opcode == SCRIPT_TYPE_FLOAT || right.opcode == SCRIPT_TYPE_FLOAT) {
            float a = as_float(left.value), b = as_float(right.value);

            r = comparison == 0 ? a == b : comparison == 1 ? a != b : comparison == 2 ? a > b
              : comparison == 3 ? a < b : comparison == 4 ? a >= b : comparison == 5 ? a <= b : 0;
        } else {
            int a = left.value, b = right.value;

            r = comparison == 0 ? a == b : comparison == 1 ? a != b : comparison == 2 ? a > b
              : comparison == 3 ? a < b : comparison == 4 ? a >= b : comparison == 5 ? a <= b : 0;
        }
        result = i == 0 ? r : join == 0 ? (result || r) : (result && r);
    }
    *cursor = c;
    return result ? 0 : skip;
}

/* Script_EvalSet: returns where the script goes on */
static const u32 *eval_set(Run *run, Locals *thread, const u32 *cursor, const u32 *end) {
    ScriptArg target, result, operand;
    int want_operator = 0, op = 0xFF, total = 0, is_float;
    float float_total = 0.0f;

    cursor++;
    target.opcode = (int)cursor[0];
    target.value = (int)cursor[1];
    read_arg(run, thread, cursor, &result);
    cursor += 2;
    is_float = result.opcode == SCRIPT_TYPE_FLOAT;
    while (cursor < end) {
        if (!want_operator) {
            read_arg(run, thread, cursor, &operand);
            cursor += 2;
            if (!is_float) {
                int v = operand.value;

                switch (op) {
                case 0xFF: total = v; break;
                case 0: total += v; break;
                case 1: total -= v; break;
                case 2: total *= v; break;
                case 3: total = v != 0 ? total / v : 0; break;
                case 4: total = v != 0 ? total % v : 0; break;
                case 5: total = (int)((u32)total << (v & 31)); break;
                case 6: total >>= (v & 31); break;
                case 7: total &= v; break;
                case 8: total |= v; break;
                }
            } else {
                float v = as_float(operand.value);

                switch (op) {
                case 0xFF: float_total = v; break;
                case 0: float_total += v; break;
                case 1: float_total -= v; break;
                case 2: float_total *= v; break;
                case 3: float_total /= v; break;
                }
            }
            op = 0xFF;
        } else {
            u32 word = *cursor;

            if ((word & 0xFF) == 0xFF) {
                cursor++;
                break;
            }
            if ((word & 0xFF) == 4) {
                op = (int)(word >> 8);
                cursor++;
            }
        }
        want_operator ^= 1;
    }
    if ((target.opcode & 0xF) >= SCRIPT_ARG_GAME_VAR && (target.opcode & 0xF) <= SCRIPT_ARG_SHARED_VAR) {
        write_var(run, thread, (u32)target.opcode, target.value, is_float ? float_bits(float_total) : total);
    }
    return cursor;
}

/* ---- The commands ---------------------------------------------------------------------------------- */
/* the spawn of an id (Entity_GetBySpawnId), or -1 */
static int spawn_of(const AutomapScriptWorld *w, int id) {
    return id >= 0 && id < 0x100 ? w->spawn_by_id[id] : -1;
}

static int object_of(const AutomapScriptWorld *w, int id) {
    return id > 0 && id < 0x100 ? w->object_by_id[id] : -1;
}

static int item_of(const AutomapScriptWorld *w, int id) {
    return id >= 0 && id < 0x100 ? w->item_by_id[id] : -1;
}

/* ScriptCmd_SetEntityFlags: the 16 flags the mask picks, from the bits of value */
static void set_entity_flags(u32 *flags, int value, int mask) {
    static const signed char bit_of[16] = {1, -1, 5, 6, 7, 18, 22, 23, 24, 25, 26, 27, 28, 14, 9, 29};
    int k;

    for (k = 0; k < 16; k++) {
        if (!(mask & (1 << k))) {
            continue;
        }
        if (k == 1) { /* bits2: 3 or 0 */
            *flags = (*flags & ~0xCu) | ((value & 2) ? 0xCu : 0u);
        } else {
            *flags = (*flags & ~(1u << bit_of[k])) | (((u32)value >> k & 1u) << bit_of[k]);
        }
    }
}

/* ScriptCmd_GetEntityFlags */
static int get_entity_flags(u32 flags) {
    static const signed char bit_of[16] = {1, -1, 5, 6, 7, 18, 22, 23, 24, 25, 26, 27, 28, 14, 9, 29};
    int k, result = 0;

    for (k = 0; k < 16; k++) {
        if (k == 1) {
            result |= ((flags >> 2) & 3) >= 2 ? 2 : 0;
        } else if (flags & (1u << bit_of[k])) {
            result |= 1 << k;
        }
    }
    return result;
}

/* whether a command never finishes in the game (it holds its thread there): ScriptCmd_Stall (command 0 and the
   unused numbers below 100) and the WaitForever ones */
static int holds_forever(int number) {
    return number == 0 || number == 4 || (number >= 11 && number <= 14) || (number >= 19 && number <= 28) ||
           (number >= 44 && number < 100) || number == 299;
}

static int run_thread(Run *run, const u32 *cursor, int depth);

/* a command at `cursor`: carried out on the world (the ones that change or read what is shown), else
   finished at once; returns 0 when it holds the thread for good */
static int command(Run *run, Locals *thread, const u32 **cursor, int depth) {
    AutomapScriptWorld *w = run->world;
    const u32 *c = *cursor;
    u32 header = *c++;
    int number = (int)(header >> 16), count = (int)(header & 0xFFFF), k, i;
    ScriptArg a[16];

    memset(a, 0, sizeof(a));
    for (k = 0; k < count; k++) {
        if (c + 2 > w->script_end) {
            return 0;
        }
        if (k < 16) {
            read_arg(run, thread, c, &a[k]);
        }
        c += 2;
    }
    *cursor = c;
    if (holds_forever(number)) {
        return 0;
    }
    switch (number) {
    case 2: /* ScriptCmd_RunScript: script_id, no_wait */
        if (a[1].value == 1) {
            if (run->queue_count < MAX_THREADS) {
                run->queue[run->queue_count++] = a[0].value;
            }
        } else if (depth < MAX_DEPTH) {
            const u32 *start = NULL, *s = w->script;

            /* Script_StartAlways: the label, from the top */
            while (s < w->script_end) {
                u32 word = *s;

                if ((word >> 16) == SCRIPT_WORD_LABEL && (int)(word & 0xFFFF) == a[0].value) {
                    start = s;
                    break;
                }
                if ((word >> 16) == SCRIPT_WORD_LABEL && (word & 0xFFFF) == SCRIPT_LABEL_END) {
                    break;
                }
                s++;
                switch (word >> 16) {
                case SCRIPT_WORD_LABEL: case SCRIPT_WORD_END: case SCRIPT_WORD_BEGIN_NO_WAIT:
                case SCRIPT_WORD_END_NO_WAIT: case SCRIPT_WORD_JUMP: break;
                case SCRIPT_WORD_IF: s += ((word & 0xFFFF) >> 8) * 5 + 1; break;
                case SCRIPT_WORD_SET: s += word & 0xFFFF; break;
                case SCRIPT_WORD_HALT: s += (word & 0xFFFF) + 1; break;
                default: s += (word & 0xFFFF) * 2; break;
                }
            }
            /* a script that never ends holds the one waiting for it too */
            if (start != NULL && !run_thread(run, start, depth + 1)) {
                return 0;
            }
        }
        break;
    case 5: /* ScriptCmd_StopAllScripts */
        run->stopped = 1;
        return 0;
    case 42: /* ScriptCmd_GetEntityCounter: target_id, first (out) */
        i = spawn_of(w, a[0].value);
        store(run, thread, &a[1], i >= 0 ? Game_GetSpawnDeathCount(w->map, i) : -1);
        break;
    case 100: /* ScriptCmd_PlaceMapItem: the fixed item `value` (MapItem_PlaceFixed) */
        i = item_of(w, a[0].value);
        if (i >= 0 && !w->item_taken[i]) {
            w->item_placed[i] = 1;
        }
        break;
    case 102: /* ScriptCmd_MoveItemTo: target_id, x, y, z (it gets there) */
        i = item_of(w, a[0].value);
        if (i >= 0) {
            w->item_position[i][0] = as_float(a[1].value);
            w->item_position[i][1] = as_float(a[2].value);
            w->item_position[i][2] = as_float(a[3].value);
        }
        break;
    case 110: /* ScriptCmd_ActivateMapObject */
    case 111: /* ScriptCmd_DeactivateMapObject */
        i = object_of(w, a[0].value);
        if (i >= 0) {
            w->object_state[i] = number == 110 ? w->object_state[i] | MAP_OBJECT_IN_USE : w->object_state[i] & ~MAP_OBJECT_IN_USE;
        }
        break;
    case 115: /* ScriptCmd_IsMapObjectOpen: target_id, first (out) */
        i = object_of(w, a[0].value);
        store(run, thread, &a[1], i >= 0 ? (w->object_state[i] & MAP_OBJECT_ON) != 0 : 0);
        break;
    case 123: /* ScriptCmd_GetItemCount: input, result (out) */
        store(run, thread, &a[1], (u32)a[0].value < 380 ? item_counts[a[0].value] : -1);
        break;
    case 150: /* ScriptCmd_SetPropSolid: target_id, first (nonzero: solid) */
        i = a[0].value >= 0 && a[0].value < 0x100 ? w->record_by_prop[a[0].value] : -1;
        if (i >= 0 && i < w->record_count) {
            w->record_hidden[i] = a[1].value == 0;
        }
        break;
    case 253: /* ScriptCmd_SetEntityFlags: target_id, value, mask */
        i = spawn_of(w, a[0].value);
        if (i >= 0) {
            set_entity_flags(&w->entity_flags[i], a[1].value, a[2].value);
        }
        break;
    case 254: /* ScriptCmd_GetEntityFlags: input, result (out) */
        i = spawn_of(w, a[0].value);
        if (i >= 0) {
            store(run, thread, &a[1], get_entity_flags(w->entity_flags[i]));
        }
        break;
    default:
        break;
    }
    return 1;
}

/* Runs a thread from `cursor` (a label) to its end: 1 when it ended, 0 when it was held for good or stopped at
   the bound */
static int run_thread(Run *run, const u32 *cursor, int depth) {
    const u32 *end = run->world->script_end;
    Locals thread;
    int words = 0;

    memset(&thread, 0, sizeof(thread));
    cursor++; /* the label */
    while (cursor != NULL && cursor < end && !run->stopped) {
        u32 word = *cursor;
        int operand = (int)(word & 0xFFFF), skip;

        if (++words > THREAD_WORDS || ++run->words > RUN_WORDS) {
            return 0;
        }
        switch (word >> 16) {
        case SCRIPT_WORD_END:
            return 1;
        case SCRIPT_WORD_LABEL: /* the next script's label: Script_Advance reads over it */
        case SCRIPT_WORD_BEGIN_NO_WAIT:
        case SCRIPT_WORD_END_NO_WAIT:
            cursor++;
            break;
        case SCRIPT_WORD_IF:
            skip = eval_if(run, &thread, &cursor);
            cursor += skip;
            break;
        case SCRIPT_WORD_JUMP:
            cursor += 1 + (s16)operand;
            break;
        case SCRIPT_WORD_SET:
            cursor = eval_set(run, &thread, cursor, end);
            break;
        case SCRIPT_WORD_HALT:
            return 0;
        default:
            if (!command(run, &thread, &cursor, depth)) {
                return 0;
            }
            break;
        }
    }
    return 0;
}

static const u32 *find_label(const AutomapScriptWorld *w, int label) {
    const u32 *s = w->script;

    while (s != NULL && s < w->script_end) {
        u32 word = *s;
        int operand = (int)(word & 0xFFFF);

        if ((word >> 16) == SCRIPT_WORD_LABEL) {
            if (operand == label) {
                return s;
            }
            if (operand == SCRIPT_LABEL_END) {
                return NULL;
            }
        }
        s++;
        switch (word >> 16) {
        case SCRIPT_WORD_LABEL: case SCRIPT_WORD_END: case SCRIPT_WORD_BEGIN_NO_WAIT:
        case SCRIPT_WORD_END_NO_WAIT: case SCRIPT_WORD_JUMP: break;
        case SCRIPT_WORD_IF: s += (operand >> 8) * 5 + 1; break;
        case SCRIPT_WORD_SET: s += operand; break;
        case SCRIPT_WORD_HALT: s += operand + 1; break;
        default: s += operand * 2; break;
        }
    }
    return NULL;
}

int Automap_RunStartScript(AutomapScriptWorld *world) {
    static Run run; /* 4 KB: not on the stack */
    const u32 *start = find_label(world, 0x7FFF);
    int k;

    if (start == NULL) {
        return 0;
    }
    memset(&run, 0, sizeof(run));
    run.world = world;
    run_thread(&run, start, 0);
    /* the threads started without waiting for them, in turn (they may start more) */
    for (k = 0; k < run.queue_count && !run.stopped; k++) {
        const u32 *s = find_label(world, run.queue[k]);

        if (s != NULL) {
            run_thread(&run, s, 1);
        }
    }
    return run.words;
}
