#include "common.h"
#include "core/game.h"

/* The camera, view and projection matrices. */

f32 view_screen_matrix[0x10] = { 0 };      /* 0x4322A0 view to screen (sceVu0ViewScreenMatrix) */

f32 D_004322E0[0x10] = { 0 };              /* 0x4322E0 */

f32 world_view_matrix[0x10] = { 0 };       /* 0x432320 world to view: the camera's position and turn undone */

f32 inverse_camera_rotation[0x10] = { 0 }; /* 0x432360 the camera rotation, inverted */

f32 D_004323A0[0x10] = { 0 };              /* 0x4323A0 */

f32 world_screen_matrix[0x60] = { 0 };     /* 0x4323E0 world to screen: view_screen_matrix times world_view_matrix */

f32 projection_matrix[0x10] = { 0 };       /* 0x432560 a perspective projection (Display_BuildProjection) */

/* 0x4325A0 the camera; the decomp had it as two arrays, the second (D_004325E8, 0x4325E8) from screen_scale on */
CameraState camera_state = { 0 };
LAYOUT_SIZE(CameraState, 0x80);
LAYOUT_OFFSET(CameraState, screen_scale, 0x48);
LAYOUT_OFFSET(CameraState, entity_shake_reach, 0x78);

f32 D_00432620[0x80] = { 0 };              /* 0x432620 */
