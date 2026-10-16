/* player_state: the player himself, PlayerState in player/player.h; player_previous the player as of the frame before. */
#include "common.h"
#include "player/player.h"

PlayerState player_state = { 0 }; /* 0x413F80 Player_ResetForNewGame clears and copies all 0x300 bytes of it */
PlayerState player_previous = { 0 }; /* 0x414280 the player as of the frame before, which Player_Move reads */
