// tetris_step.cpp
#include "tetris_step.h"
#include <cstdint>


// pieceをその場に固定した BitBoard を作る（bbコピーして必要行だけ書き換え）
static inline BitBoard place_piece_bb(
    const BitBoard &bb,
    PieceType piece,
    int rot,
    int x,
    int y)
{
    BitBoard out = bb;

    const PieceBB &info = PIECE_BB[(int)piece][rot & 3];
    const int base_x = x + info.minx;
    const int base_y = y + info.miny;

    // 呼び出し側で is_valid_position を通している前提（ここでは範囲チェックしない）
    for (int dy = 0; dy < info.h; ++dy)
    {
        BoardBits mask = (info.rowmask[dy] << base_x);
        out.row[base_y + dy] |= mask;
    }
    return out;
}

// ライン消去（BitBoard版）
// y=0 が下、yが増えるほど上、という今の実装前提に合わせる
static inline int clear_lines_bb(
    BitBoard &bb,
    int base_y,
    int height) // ピースの高さ（最大4）
{
    int cleared = 0;

    for (int i = 0; i < height; ++i)
    {
        int y = base_y + i;

        if (bb.row[y] == FULL_ROW)
        {
            ++cleared;

            // 上を1行ずつ詰める（最大40コピーだが回数は最大4回）
            for (int k = y; k + 1 < TOTAL_BOARD_HEIGHT; ++k)
                bb.row[k] = bb.row[k + 1];

            bb.row[TOTAL_BOARD_HEIGHT - 1] = 0;

            --i;      // 同じ位置を再チェック
            --height; // 有効高さ1減少
        }
    }

    return cleared;
}

static inline bool is_perfect_clear_bb(const BitBoard &bb)
{
    for (int y = 0; y < TOTAL_BOARD_HEIGHT; ++y)
        if (bb.row[y] != 0)
            return false;
    return true;
}

StepResult step_lock_piece_at(
    const BitBoard &bb,
    PieceType piece,
    int rot,
    int x,
    int y)
{
    StepResult r{};
    r.final_y = y;

    // =========================================================
    // 1) copy board
    // =========================================================
    BitBoard placed = bb;

    // =========================================================
    // 2) place piece (touches at most 4 rows)
    // =========================================================
    const auto &info = PIECE_BB[(int)piece][rot & 3];

    const int base_x = x + info.minx;
    const int base_y = y + info.miny;

    for (int dy = 0; dy < info.h; ++dy)
        placed.row[base_y + dy] |= (info.rowmask[dy] << base_x);

    // =========================================================
    // 3) clear lines (local h + 1 rows)
    // =========================================================
    int cleared = 0;

    // 影響範囲：ピースが触れた行 ＋ 直上1行
    int y0 = base_y;
    int y1 = base_y + info.h; // ← +1 行が重要

    if (y0 < 0)
        y0 = 0;
    if (y1 >= TOTAL_BOARD_HEIGHT)
        y1 = TOTAL_BOARD_HEIGHT - 1;

    for (int row = y0; row <= y1;)
    {
        if ((placed.row[row] & FULL_ROW) == FULL_ROW)
        {
            ++cleared;

            // shift down
            for (int k = row; k + 1 < TOTAL_BOARD_HEIGHT; ++k)
                placed.row[k] = placed.row[k + 1];

            placed.row[TOTAL_BOARD_HEIGHT - 1] = 0;

            // 同じ row を再チェック（上から落ちてきた行）
            // row++ しない
        }
        else
        {
            ++row;
        }
    }

    r.lines_cleared = cleared;

    // =========================================================
    // 4) perfect clear
    // =========================================================
    if (cleared > 0)
        r.perfect_clear = is_perfect_clear_bb(placed);
    else
        r.perfect_clear = false;

    r.bb_after = placed;
    return r;
}
