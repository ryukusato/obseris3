// tetris_board.h
#pragma once
#include <vector>
#include "tetris_piece.h"

constexpr int BOARD_WIDTH = 10;
constexpr int TOTAL_BOARD_HEIGHT = 40;

using Board = std::vector<std::vector<int>>;

struct BitBoard
{
    uint16_t row[40];
};

BitBoard make_bitboard(const Board &);

constexpr BoardBits FULL_ROW = (BoardBits(1) << BOARD_WIDTH) - 1;

// ★探索・衝突判定はこれだけ
bool is_valid_position(
    const BitBoard &bb,
    PieceType piece,
    int rot,
    int x,
    int y);

// ロック用（評価フェーズ）
Board place_piece(
    const Board &,
    PieceType piece,
    int rot,
    int x,
    int y);
