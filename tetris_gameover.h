// tetris_gameover.h
#pragma once
#include "tetris_board.h"
#include "tetris_rules.h"

// ガイドライン準拠トップアウト判定
bool is_dead(const BitBoard &bb, PieceType p);

