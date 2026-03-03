// tetris_rules.cpp
#include "tetris_rules.h"
#include <array>
#include "tetris_board.h"


static inline int norm_rot(int r){ r&=3; if(r<0) r+=4; return r; }



// ===== SRS kicks（dyは下が-1系に合わせ済み）=====
static constexpr KickList K_O = {{0}, {0}, 1};

static constexpr KickList K_JLSTZ[8] = {
    {{0, -1, -1, 0, -1}, {0, 0, 1, -2, -2}, 5},
    {{0, 1, 1, 0, 1}, {0, 0, -1, 2, 2}, 5},
    {{0, 1, 1, 0, 1}, {0, 0, -1, 2, 2}, 5},
    {{0, -1, -1, 0, -1}, {0, 0, 1, -2, -2}, 5},
    {{0, 1, 1, 0, 1}, {0, 0, 1, -2, -2}, 5},
    {{0, -1, -1, 0, -1}, {0, 0, -1, 2, 2}, 5},
    {{0, -1, -1, 0, -1}, {0, 0, -1, 2, 2}, 5},
    {{0, 1, 1, 0, 1}, {0, 0, 1, -2, -2}, 5},
};

static constexpr KickList K_I[8] = {
    {{0, -2, 1, -2, 1}, {0, 0, 0, -1, 2}, 5},
    {{0, 2, -1, 2, -1}, {0, 0, 0, 1, -2}, 5},
    {{0, -1, 2, -1, 2}, {0, 0, 0, 2, -1}, 5},
    {{0, 1, -2, 1, -2}, {0, 0, 0, -2, 1}, 5},
    {{0, 2, -1, 2, -1}, {0, 0, 0, 1, -2}, 5},
    {{0, -2, 1, -2, 1}, {0, 0, 0, -1, 2}, 5},
    {{0, 1, -2, 1, -2}, {0, 0, 0, -2, 1}, 5},
    {{0, -1, 2, -1, 2}, {0, 0, 0, 2, -1}, 5},
};

static inline int kick_index(int f,int t){
    f = norm_rot(f);
    t = norm_rot(t);
    if(f==0&&t==1)return 0; if(f==1&&t==0)return 1;
    if(f==1&&t==2)return 2; if(f==2&&t==1)return 3;
    if(f==2&&t==3)return 4; if(f==3&&t==2)return 5;
    if(f==3&&t==0)return 6; if(f==0&&t==3)return 7;
    return -1;
}

const KickList &get_kick(
    PieceType p, int from_rot, int to_rot)
{
    if (p == PieceType::O)
        return K_O;

    int idx = kick_index(from_rot, to_rot);
    if (idx < 0)
        return K_O;

    return (p == PieceType::I) ? K_I[idx] : K_JLSTZ[idx];
}

std::pair<int, int> spawn_position_with_fallback(const BitBoard &bb, PieceType piece)
{
    int x = 4;
    int y = 20;

    if (is_valid_position(bb, piece, 0, x, y))
        return {x, y};
    if (is_valid_position(bb, piece, 0, x, y + 1))
        return {x, y + 1};
    return {-1, -1};
}