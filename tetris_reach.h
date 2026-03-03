// tetris_reach.h
#pragma once
#include "tetris_core.h"
#include "tetris_rules.h"


// スポーン姿勢 (start_x, start_y, start_rot)
// から
// 目標姿勢   (target_x, target_y, target_rot)
// に、移動・落下・回転(SRSキック込み)だけで
// 到達できるかを BFS で判定する。
bool can_reach(const Board& board,
               PieceType piece,
               int start_x, int start_y, int start_rot,
               int target_x, int target_y, int target_rot);
