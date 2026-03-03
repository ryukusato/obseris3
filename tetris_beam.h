#pragma once
#include <vector>
#include <utility>

#include "tetris_state.h"
#include "tetris_eval.h"
#include "tetris_search.h"



// ----------------------------
// ビームサーチ設定
// ----------------------------
struct BeamConfig
{
    int ply = 5;         // search depth
    int beam_width = 12; // nodes kept per layer
    int local_width = 5;
    bool use_landing_reward = true;
};

struct BeamResult
{
    Landing best;
    std::vector<std::pair<double, Landing>> root_topk;
    double think_ms;
};

// ----------------------------
// API
// ----------------------------
BeamResult beam_search(
    const GameState &root,
    const EvalWeights &w,
    const BeamConfig &cfg);
