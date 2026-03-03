#pragma once
#include "tetris_board.h"
#include "tetris_piece.h"

struct StepResult
{
    BitBoard bb_after; // ★ BoardではなくBitBoard
    int final_y;
    int lines_cleared;
    bool perfect_clear = false;
};

// 高速版（BitBoard入力）
StepResult step_lock_piece_at(
    const BitBoard &bb,
    PieceType piece,
    int rot,
    int x,
    int y);

// 必要なときだけ Board に戻す
Board bitboard_to_board(const BitBoard &bb);
