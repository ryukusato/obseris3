#pragma once
#include "tetris_board.h"
#include "tetris_rules.h"

enum class Winner {
    None,
    Player1,
    Player2,
    Draw
};

Winner judge_winner(const BitBoard& b1, PieceType p1,
                    const BitBoard& b2, PieceType p2);
