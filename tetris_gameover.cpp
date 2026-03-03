//tetris_gameover.cpp
#include "tetris_gameover.h"

bool is_dead(const BitBoard &bb, PieceType p)
{
    auto [sx, sy] = spawn_position_with_fallback(bb, p);
    return sx < 0;
}
