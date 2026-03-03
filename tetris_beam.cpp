// ============================================================
// 1-Player Beam Search (Search LandingSearch, Output Landing)
// Engine logic only (NO game progression side-effects)
// Python adapter is responsible for actual match execution.
// ============================================================

#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstdint>

#include "tetris_state.h"
#include "tetris_eval.h"
#include "tetris_beam.h"
#include "tetris_engine.h"
#include "tetris_step.h"
#include "tetris_search.h" // Landing / LandingSearch / enumerate_landings_search / legal_moves

// ------------------------------
// helpers
// ------------------------------
static inline double evaluate_landing_search(const LandingSearch &ls,
                                             const EvalWeights &w)
{
    Landing tmp{};
    tmp.lines_cleared = ls.lines_cleared;
    tmp.kind = ls.kind;
    tmp.combo = ls.combo_after;
    tmp.back_to_back = ls.b2b_after;
    tmp.perfect_clear = ls.perfect_clear;
    tmp.attack = ls.attack;
    tmp.used_t_piece = (ls.piece == PieceType::T);
    return (double)evaluate_landing(tmp, w);
}

static inline GameState apply_search_move(GameState s, const LandingSearch &ls)
{
    // board
    s.bb = ls.bb_after;

    // state values AFTER this move
    s.combo = ls.combo_after;
    s.back_to_back = ls.b2b_after;

    s.has_hold = ls.has_hold_after;
    s.hold_piece = ls.hold_piece_after;

    s.current = ls.current_after;
    s.bag_index = ls.bag_index_after;

    s.spawn_x = ls.spawn_x_after;
    s.spawn_y = ls.spawn_y_after;
    s.dead = ls.dead_after;

    s.used_hold_this_turn = ls.used_hold_this_turn_after; // usually false
    return s;
}

// 「Landing(フル)がどのピースを置いたか」
// 既存 Landing 側に piece_after_hold を持ってる前提（あなたの定義）
static inline PieceType placed_piece_of(const Landing &L)
{
    return L.used_hold ? L.piece_after_hold : L.piece;
}

static inline bool bitboard_equal(const BitBoard &a, const BitBoard &b)
{
    // TOTAL_BOARD_HEIGHT 行分を比較（rowが固定長array想定）
    for (int y = 0; y < TOTAL_BOARD_HEIGHT; ++y)
        if (a.row[y] != b.row[y])
            return false;
    return true;
}

// ------------------------------------------------------------
// Core 1-player beam search
// ------------------------------------------------------------
BeamResult beam_search(
    const GameState &root,
    const EvalWeights &w,
    const BeamConfig &cfg)
{
    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();

    BeamResult out{};
    out.root_topk.clear();
    out.think_ms = 0.0;

    // --------------------------------------
    // enumerate first moves (search version)
    // --------------------------------------
    const std::vector<LandingSearch> first_moves = enumerate_landings_search(root);
    if (first_moves.empty())
        return out;

    struct Node
    {
        GameState s;
        double score;
    };

    std::vector<std::pair<double, LandingSearch>> root_scores;
    root_scores.reserve(first_moves.size());

    // =========================================================
    // Cold-Clear style: independent beam per first move
    // =========================================================
    for (const auto &first : first_moves)
    {
        GameState s1 = apply_search_move(root, first);

        const double r1 = cfg.use_landing_reward ? evaluate_landing_search(first, w) : 0.0;
        const double h1 = (double)evaluate_board(s1.bb, w);

        std::vector<Node> beam;
        beam.reserve((size_t)cfg.local_width);
        beam.push_back({s1, r1 + h1});

        // ---------- deeper ply ----------
        for (int d = 1; d < cfg.ply; ++d)
        {
            std::vector<Node> next;
            // ここは過剰確保でも幅が小さいので誤差
            next.reserve(beam.size() * 64);

            for (const auto &node : beam)
            {
                if (node.s.dead)
                    continue;

                const std::vector<LandingSearch> moves = enumerate_landings_search(node.s);
                for (const auto &m : moves)
                {
                    GameState ns = apply_search_move(node.s, m);

                    const double r = cfg.use_landing_reward ? evaluate_landing_search(m, w) : 0.0;
                    const double h = (double)evaluate_board(ns.bb, w);

                    // ★重要：累積
                    next.push_back({ns, node.score + r + h});
                }
            }

            if (next.empty())
                break;

            std::sort(next.begin(), next.end(),
                      [](const Node &a, const Node &b)
                      { return a.score > b.score; });

            if ((int)next.size() > cfg.local_width)
                next.resize(cfg.local_width);

            beam.swap(next);
        }

        // best leaf propagates to root
        double best_leaf = -1e18;
        for (const auto &n : beam)
            if (n.score > best_leaf)
                best_leaf = n.score;

        root_scores.push_back({best_leaf, first});
    }

    // --------------------------------------
    // root top-k
    // --------------------------------------
    std::sort(root_scores.begin(), root_scores.end(),
              [](const auto &a, const auto &b)
              { return a.first > b.first; });

    if ((int)root_scores.size() > cfg.beam_width)
        root_scores.resize(cfg.beam_width);

    // --------------------------------------
    // Output needs Landing(with path)
    // So enumerate full first moves once
    // --------------------------------------
    const std::vector<Landing> full_first = legal_moves(root);

    auto match_full = [&](const LandingSearch &ls) -> const Landing *
    {
        // 強い一致：hold / placed_piece / x / rot (+y) / bb_after
        // まず完全一致を探す
        for (const auto &L : full_first)
        {
            if (L.used_hold != ls.used_hold)
                continue;
            if (placed_piece_of(L) != ls.piece)
                continue;
            if (L.final_x != ls.final_x)
                continue;
            if (L.final_rot != ls.final_rot)
                continue;
            if (L.final_y != ls.final_y)
                continue;

            // bb_afterが取れるなら一致も要求（ズレ防止）
            if (!bitboard_equal(L.bb_after, ls.bb_after))
                continue;

            return &L;
        }

        // 次に y を緩める（kick/落下実装差の吸収）
        const Landing *best = nullptr;
        for (const auto &L : full_first)
        {
            if (L.used_hold != ls.used_hold)
                continue;
            if (placed_piece_of(L) != ls.piece)
                continue;
            if (L.final_x != ls.final_x)
                continue;
            if (L.final_rot != ls.final_rot)
                continue;

            if (bitboard_equal(L.bb_after, ls.bb_after))
                return &L;

            if (!best)
                best = &L;
        }
        return best;
    };

    out.root_topk.reserve(root_scores.size());

    for (const auto &p : root_scores)
    {
        const double sc = p.first;
        const LandingSearch &ls = p.second;

        const Landing *full = match_full(ls);
        if (full)
        {
            out.root_topk.emplace_back(sc, *full); // copy includes path
        }
        else
        {
            // fallback (should rarely happen)
            Landing L{};
            L.piece = ls.piece;
            L.final_x = ls.final_x;
            L.final_y = ls.final_y;
            L.final_rot = ls.final_rot;
            L.used_hold = ls.used_hold;

            L.bb_after = ls.bb_after;
            L.lines_cleared = ls.lines_cleared;
            L.perfect_clear = ls.perfect_clear;
            L.kind = ls.kind;
            L.attack = ls.attack;
            L.combo = ls.combo_after;
            L.back_to_back = ls.b2b_after;

            out.root_topk.emplace_back(sc, L);
        }
    }

    if (!out.root_topk.empty())
        out.best = out.root_topk.front().second;

    auto t1 = Clock::now();
    out.think_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    print_enum_stats();
    return out;
}
