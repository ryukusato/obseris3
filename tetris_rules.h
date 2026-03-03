// tetris_rules.h
#pragma once
#include <vector>
#include <utility>
#include "tetris_piece.h"
#include "tetris_board.h"

// SRS wall kick
struct KickList
{
    int dx[5];
    int dy[5];
    int n;
};
const KickList &get_kick(PieceType p, int from_rot, int to_rot);

// スポーン基準位置（衝突判定はしない）
inline std::pair<int, int> spawn_base_position()
{
    return {4, 20};
}
std::pair<int, int> spawn_position_with_fallback(const BitBoard &bb, PieceType piece);