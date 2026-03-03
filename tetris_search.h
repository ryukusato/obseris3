//  tetris_search.h
#pragma once
#include "tetris_rules.h"
#include <vector>

struct GameState;
enum class Action
{
    None,
    MoveLeft,
    MoveRight,
    SoftDrop,
    HardDrop, // ★追加（後で末尾圧縮用）
    RotateCW,
    RotateCCW,
    Hold
};

enum class ClearKind
{
    None,
    Clear1,
    Clear2,
    Clear3,
    Clear4,
    Tspin1,
    Tspin2,
    Tspin3,
    MiniTspin1,
    MiniTspin2
};

struct Landing {

    BitBoard bb_after; // ←これだけ持つ

    int final_x, final_y, final_rot;
    int lines_cleared;
    int combo;
    PieceType piece;
    std::vector<Action> path;

    ClearKind kind = ClearKind::None;
    bool used_t_piece = false;
    bool perfect_clear = false;
    bool back_to_back = false; // これは後でゲーム状態側から渡してもOK
    bool used_hold = false;
    PieceType piece_after_hold;
    int attack = 0;
    uint8_t last_is_rot;
    uint8_t last_rot_dir;
    Landing() = default;
};

struct LandingSearch
{
    // ===== 手の識別（rootのtopk照合用）=====
    PieceType piece; // 実際に置いたピース（hold後）
    int final_x;
    int final_y;
    int final_rot;
    bool used_hold;

    // ===== 盤面結果（ロック後）=====
    BitBoard bb_after;
    int lines_cleared;
    bool perfect_clear;

    // ===== 報酬/評価用 =====
    ClearKind kind;
    int attack;

    // ===== 状態遷移用（この手の後の state を固定）=====
    int combo_after;
    bool b2b_after;

    bool has_hold_after;
    PieceType hold_piece_after;

    PieceType current_after; // 次ターンの current
    int spawn_x_after;
    int spawn_y_after;
    bool dead_after;

    int bag_index_after;            // ★重要：bagの消費位置
    bool used_hold_this_turn_after; // 通常 false

    // ===== optional：path復元用 =====
    int node_idx;
};

std::vector<LandingSearch>
enumerate_landings_search(const GameState &s);
void compress_softdrop(std::vector<Action> &p);
std::vector<std::tuple<int, int, int, bool>>
enumerate_landings_positions_virtual(const BitBoard &bb, PieceType piece, int spawn_x, int spawn_y);

std::vector<Landing> enumerate_landings(const BitBoard &bb,
                                            PieceType piece,
                                            int spawn_x,
                                            int spawn_y,
                                            int current_combo,
                                            bool current_b2b);
void print_enum_stats();
ClearKind judge_clear_kind(
    const BitBoard &bb_before,
    PieceType piece,
    int final_x,
    int final_y,
    int final_rot,
    int lines_cleared,
    bool last_rot);