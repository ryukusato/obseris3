#pragma once
#include "tetris_board.h"
#include "tetris_search.h"
#include <array>

struct EvalWeights
{
    // --- Transient (盤面評価) ---
    int height = -39;
    int bumpiness = -24;
    int bumpiness_sq = -7;
    int row_trans = -5;
    int covered = -17;
    int covered_sq = -1;

    int cavity_cells = -173;
    int cavity_cells_sq = -3;
    int overhang_cells = -34;
    int overhang_cells_sq = -1;

    int top_half = -150;    // height > 10 penalty
    int top_quarter = -511; // height > 15 penalty
    int jeopardy = -11;     // risk factor

    int well_depth = 57;
    int max_well_cap = 17;
    std::array<int, 10> well_column = {20, 23, 20, 50, 59, 21, 59, 10, -10, 24};

    // T-Slot detection rewards (0, 1, 2, 3 lines)
    std::array<int, 4> tslot = {8, 148, 192, 407};

    // --- Reward (行動評価) ---
    int b2b_clear = 104;
    int clear1 = -143;
    int clear2 = -100;
    int clear3 = -58;
    int clear4 = 390;

    int tspin1 = 121;
    int tspin2 = 410;
    int tspin3 = 602;
    int mini_tspin1 = -158;
    int mini_tspin2 = -93;

    int perfect_clear = 999;
    int combo_bonus = 150; // CC calls this combo_garbage multiplier
    int wasted_t = -152;
    int move_time = -3; // per frame approximation
};

// ヘルパー: 列の高さを取得
std::array<int, BOARD_WIDTH> column_heights(const BitBoard &bb);

// 盤面評価 (CCのTransient Eval相当)
int evaluate_board(const GameState &gs, const EvalWeights &w);

// 行動評価 (CCのReward相当)
int evaluate_landing(const Landing &l, const EvalWeights &w);