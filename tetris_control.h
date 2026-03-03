#pragma once
#include "tetris_board.h"
#include "tetris_piece.h"

// 左右移動 (dx = -1 or +1)
bool try_move(const BitBoard &bb,
              PieceType piece,
              int rot,
              int &x, int &y,
              int dx);

// ソフトドロップ1マス
bool try_soft_drop(const BitBoard &bb,
                   PieceType piece,
                   int rot,
                   int &x, int &y);

// ハードドロップ先の y を求める
int hard_drop_y(const BitBoard &bb,
                PieceType piece,
                int rot,
                int x,
                int start_y);
