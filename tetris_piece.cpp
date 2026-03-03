// tetris_piece.cpp
#include "tetris_piece.h"
#include <algorithm>
#include <stdexcept>

// ===== Coords 定義 =====
static const std::array<Coords, 4> SHAPE_I = {{
    {{-1, 0}, {0, 0}, {1, 0}, {2, 0}},
    {{1, -1}, {1, 0}, {1, 1}, {1, 2}},
    {{-1, 1}, {0, 1}, {1, 1}, {2, 1}},
    {{0, -1}, {0, 0}, {0, 1}, {0, 2}},
}};
static const std::array<Coords, 4> SHAPE_O = {{
    {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
    {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
    {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
    {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
}};
static const std::array<Coords, 4> SHAPE_T = {{
    {{-1, 0}, {0, 0}, {1, 0}, {0, 1}},
    {{0, -1}, {0, 0}, {0, 1}, {1, 0}},
    {{-1, 0}, {0, 0}, {1, 0}, {0, -1}},
    {{0, -1}, {0, 0}, {0, 1}, {-1, 0}},
}};
static const std::array<Coords, 4> SHAPE_S = {{
    {{-1, 0}, {0, 0}, {0, 1}, {1, 1}},
    {{0, 1}, {0, 0}, {1, 0}, {1, -1}},
    {{-1, -1}, {0, -1}, {0, 0}, {1, 0}},
    {{-1, 1}, {-1, 0}, {0, 0}, {0, -1}},
}};
static const std::array<Coords, 4> SHAPE_Z = {{
    {{-1, 1}, {0, 1}, {0, 0}, {1, 0}},
    {{1, 1}, {1, 0}, {0, 0}, {0, -1}},
    {{-1, 0}, {0, 0}, {0, -1}, {1, -1}},
    {{0, 1}, {0, 0}, {-1, 0}, {-1, -1}},
}};
static const std::array<Coords, 4> SHAPE_J = {{
    {{-1, 0}, {0, 0}, {1, 0}, {-1, 1}},
    {{0, -1}, {0, 0}, {0, 1}, {1, 1}},
    {{-1, 0}, {0, 0}, {1, 0}, {1, -1}},
    {{0, -1}, {0, 0}, {0, 1}, {-1, -1}},
}};
static const std::array<Coords, 4> SHAPE_L = {{
    {{-1, 0}, {0, 0}, {1, 0}, {1, 1}},
    {{0, -1}, {0, 0}, {0, 1}, {1, -1}},
    {{-1, 0}, {0, 0}, {1, 0}, {-1, -1}},
    {{0, -1}, {0, 0}, {0, 1}, {-1, 1}},
}};

// ===== 全ピース形状テーブル =====
const std::array<std::array<Coords, 4>, PieceTypeCount> SHAPES = {{SHAPE_I, SHAPE_O, SHAPE_T,
                                                                   SHAPE_S, SHAPE_Z, SHAPE_J, SHAPE_L}};

// ===== bitboard用情報 =====
PieceBB PIECE_BB[PieceTypeCount][4];

static inline int norm_rot(int r)
{
    r &= 3;
    return r;
}

const Coords &get_shape(PieceType p, int rot)
{

    return SHAPES[(int)p][norm_rot(rot)];
}

// ===== 初期化（mainで1回呼ぶ） =====
void init_piece_tables()
{
    for (int p = 0; p < PieceTypeCount; p++)
    {
        for (int r = 0; r < 4; r++)
        {
            const Coords &sh = SHAPES[p][r];

            int minx = 99, miny = 99, maxx = -99, maxy = -99;
            for (auto [dx, dy] : sh)
            {
                minx = std::min(minx, dx);
                miny = std::min(miny, dy);
                maxx = std::max(maxx, dx);
                maxy = std::max(maxy, dy);
            }

            PieceBB info{};
            info.minx = minx;
            info.miny = miny;
            info.w = maxx - minx + 1;
            info.h = maxy - miny + 1;

            for (int i = 0; i < 4; i++)
                info.rowmask[i] = 0;

            for (auto [dx, dy] : sh)
            {
                int x = dx - minx;
                int y = dy - miny;
                info.rowmask[y] |= (BoardBits(1) << x);
            }

            PIECE_BB[p][r] = info;
        }
    }
}
