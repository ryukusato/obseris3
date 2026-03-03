#include "tetris_control.h"
#include "tetris_rules.h" // is_valid_position

// ===============================
// 左右移動
// ===============================
bool try_move(const BitBoard &bb,
              PieceType piece,
              int rot,
              int &x, int &y,
              int dx)
{
    int nx = x + dx;

    if (is_valid_position(bb, piece, rot, nx, y))
    {
        x = nx;
        return true;
    }
    return false;
}

// ===============================
// ソフトドロップ（1マス）
// ===============================
bool try_soft_drop(const BitBoard &bb,
                   PieceType piece,
                   int rot,
                   int &x, int &y)
{
    int ny = y - 1;

    if (is_valid_position(bb, piece, rot, x, ny))
    {
        y = ny;
        return true;
    }
    return false;
}

// ===============================
// ハードドロップ（最終yのみ計算）
// ===============================
int hard_drop_y(const BitBoard &bb,
                PieceType piece,
                int rot,
                int x,
                int start_y)
{
    int y = start_y;

    // 下に行けるだけ行く
    while (is_valid_position(bb, piece, rot, x, y - 1))
    {
        --y;
    }

    return y;
}
