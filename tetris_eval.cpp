#include "tetris_eval.h"
#include "tetris_step.h"
#include "tetris_state.h"
#include <algorithm>
#include <cmath>
#include <vector>

// ----------------------------------------------------------------------
// Utils
// ----------------------------------------------------------------------

static std::array<int, BOARD_WIDTH> get_heights(const BitBoard &bb)
{
    std::array<int, BOARD_WIDTH> h{};
    for (int x = 0; x < BOARD_WIDTH; ++x)
    {
        int y;
        for (y = TOTAL_BOARD_HEIGHT - 1; y >= 0; --y)
        {
            if ((bb.row[y] >> x) & 1)
                break;
        }
        h[x] = y + 1;
    }
    return h;
}

static inline bool occupied(const BitBoard &bb, int x, int y)
{
    if (x < 0 || x >= BOARD_WIDTH || y < 0)
        return true;
    if (y >= TOTAL_BOARD_HEIGHT)
        return false;
    return (bb.row[y] >> x) & 1;
}

// ----------------------------------------------------------------------
// Pattern Matching (CC Port)
// ----------------------------------------------------------------------

struct TSlotMatch
{
    int x, y, r;
    bool found;
};

// --- Sky & Twist (既存) ---

static TSlotMatch sky_tslot_right(const BitBoard &bb, const std::array<int, 10> &h)
{
    for (int x = 0; x <= 7; ++x)
    {
        if (h[x + 1] <= h[x + 2] - 1)
        {
            int y = h[x + 2];
            if (occupied(bb, x, y) && !occupied(bb, x, y - 1) && occupied(bb, x, y - 2))
            {
                return {x + 1, h[x + 2], 2, true}; // South
            }
        }
    }
    return {0, 0, 0, false};
}

static TSlotMatch sky_tslot_left(const BitBoard &bb, const std::array<int, 10> &h)
{
    for (int x = 0; x <= 7; ++x)
    {
        if (h[x + 1] <= h[x] - 1)
        {
            int y = h[x];
            if (occupied(bb, x + 2, y) && !occupied(bb, x + 2, y - 1) && occupied(bb, x + 2, y - 2))
            {
                return {x + 1, h[x], 2, true}; // South
            }
        }
    }
    return {0, 0, 0, false};
}

static TSlotMatch tst_twist_left(const BitBoard &bb, const std::array<int, 10> &h)
{
    for (int x = 0; x <= 7; ++x)
    {
        int h2 = h[x + 1];
        if (h[x] <= h2)
        {
            // Check strict occupancy as CC
            int y = h2 + 1;
            if (occupied(bb, x + 2, y) && !occupied(bb, x + 2, y - 1) && !occupied(bb, x + 2, y - 2) &&
                !occupied(bb, x + 1, y - 3) && !occupied(bb, x + 2, y - 4) &&
                occupied(bb, x - 1, h2) == occupied(bb, x - 1, h2 + 1)) // check wall uniformity
            {
                return {x + 2, h2 - 2, 3, true}; // West
            }
        }
    }
    return {0, 0, 0, false};
}

static TSlotMatch tst_twist_right(const BitBoard &bb, const std::array<int, 10> &h)
{
    for (int x = 0; x <= 7; ++x)
    {
        int h1 = h[x + 1];
        if (h[x + 2] <= h1)
        {
            int y = h1 + 1;
            if (occupied(bb, x, y) && !occupied(bb, x, y - 1) && !occupied(bb, x, y - 2) &&
                !occupied(bb, x + 1, y - 3) && !occupied(bb, x, y - 4) &&
                occupied(bb, x + 3, h1) == occupied(bb, x + 3, h1 + 1))
            {
                return {x, h1 - 2, 1, true}; // East
            }
        }
    }
    return {0, 0, 0, false};
}

// --- Fin (新規追加) ---

static TSlotMatch fin_left(const BitBoard &bb, const std::array<int, 10> &h)
{
    // heights [h1 h2 _ _], require h1 <= h2+1
    for (int x = 0; x <= 6; ++x)
    {
        if (h[x] <= h[x + 1] + 1)
        {
            int y = h[x + 1] + 2; // start_y
            // [? ? # # ?] x+2, y (check x+2, x+3 occupied)
            // [? ? _ _ ?] y-1 (check x+2, x+3 empty)
            // [? ? _ _ #] y-2 (check x+2, x+3 empty, x+4 occupied) -- simplified check
            // CC check:
            // x+2,y: #, x+3,y: #
            // x+2,y-1: _, x+3,y-1: _
            // x+2,y-2: _, x+3,y-2: _
            // x+2,y-4: #, x+4,y-4: # (Fin base)

            if (occupied(bb, x + 2, y) && occupied(bb, x + 3, y) &&
                !occupied(bb, x + 2, y - 1) && !occupied(bb, x + 3, y - 1) &&
                !occupied(bb, x + 2, y - 2) && !occupied(bb, x + 3, y - 2) &&
                occupied(bb, x + 2, y - 4) && occupied(bb, x + 4, y - 4))
            {
                return {x + 3, h[x + 1] - 1, 3, true}; // West
            }
        }
    }
    return {0, 0, 0, false};
}

static TSlotMatch fin_right(const BitBoard &bb, const std::array<int, 10> &h)
{
    // heights [_ _ h1 h2], require h2 <= h1+1
    for (int x = 0; x <= 6; ++x)
    {
        if (h[x + 3] <= h[x + 2] + 1)
        {
            int y = h[x + 2] + 2;
            if (occupied(bb, x, y) && occupied(bb, x + 1, y) &&
                !occupied(bb, x, y - 1) && !occupied(bb, x + 1, y - 1) &&
                !occupied(bb, x, y - 2) && !occupied(bb, x + 1, y - 2) &&
                occupied(bb, x + 1, y - 4) && occupied(bb, x - 1, y - 4))
            {
                return {x, h[x + 2] - 1, 1, true}; // East
            }
        }
    }
    return {0, 0, 0, false};
}

// --- Cave (新規追加: 洞窟探索) ---
// TST Twistから派生する深い穴の探索

static TSlotMatch cave_tslot(const BitBoard &bb, const TSlotMatch &tst)
{
    // tst.x, tst.y は TST Twist で見つかった "Tの回転中心"
    // ここから T-Spin で入った後の形 (Sonic Drop相当) を計算
    // TST Twist の結果座標はすでに「底」にあるので、そのままチェック

    int x = tst.x;
    int y = tst.y;

    // TST Twist の r は 1(East) か 3(West)
    if (tst.r == 1)
    { // East
        // Check East Logic from CC
        // Check surrounding blocks to ensure it's a valid cave
        if (!occupied(bb, x - 1, y) && occupied(bb, x - 1, y - 1) && occupied(bb, x + 1, y - 1) && occupied(bb, x - 1, y + 1))
        {
            return {x, y, 2, true}; // South
        }
        else if (!occupied(bb, x + 1, y - 1) && !occupied(bb, x + 2, y - 1) && !occupied(bb, x + 1, y - 2) &&
                 occupied(bb, x - 1, y) && occupied(bb, x + 2, y) && occupied(bb, x, y - 2) && occupied(bb, x + 2, y - 2))
        {
            return {x + 1, y - 1, 2, true}; // South (shifted)
        }
    }
    else if (tst.r == 3)
    { // West
        if (!occupied(bb, x + 1, y) && occupied(bb, x + 1, y + 1) && occupied(bb, x + 1, y - 1) && occupied(bb, x - 1, y - 1))
        {
            return {x, y, 2, true}; // South
        }
        else if (!occupied(bb, x - 1, y - 1) && !occupied(bb, x - 2, y - 1) && !occupied(bb, x - 1, y - 2) &&
                 occupied(bb, x + 1, y) && occupied(bb, x - 2, y) && occupied(bb, x - 2, y - 2) && occupied(bb, x, y - 2))
        {
            return {x - 1, y - 1, 2, true}; // South (shifted)
        }
    }

    return {0, 0, 0, false};
}

// ----------------------------------------------------------------------
// Simulation
// ----------------------------------------------------------------------

struct CutoutResult
{
    bool success;
    BitBoard bb;
    int lines;
};

static CutoutResult cutout_tslot(const BitBoard &bb, int x, int y, int r)
{
    if (occupied(bb, x, y))
        return {false, bb, 0};

    StepResult res = step_lock_piece_at(bb, PieceType::T, r, x, y);
    // T-spin check logic is skipped as we assume the pattern implies T-spin
    // CC does check placement_kind strictly, but patterns usually guarantee it.

    return {true, res.bb_after, res.lines_cleared};
}

// ----------------------------------------------------------------------
// Features
// ----------------------------------------------------------------------

static int row_transitions(const BitBoard &bb)
{
    int t = 0;
    for (int y = 0; y < TOTAL_BOARD_HEIGHT; ++y)
    {
        uint32_t r = bb.row[y] & FULL_ROW;
        uint32_t w = (1u << 0) | (r << 1) | (1u << (BOARD_WIDTH + 1));
        uint32_t diff = w ^ (w >> 1);
        t += __builtin_popcount(diff);
    }
    return t;
}

static std::pair<int, int> bumpiness(const std::array<int, BOARD_WIDTH> &h, int well)
{
    int bump = 0;
    int bump_sq = 0;
    int prev = (well == 0 ? 1 : 0);
    for (int i = 1; i < BOARD_WIDTH; ++i)
    {
        if (i == well)
            continue;
        int d = std::abs(h[prev] - h[i]);
        bump += d;
        bump_sq += d * d;
        prev = i;
    }
    return {bump, bump_sq};
}

static std::pair<int, int> cavities_and_overhangs(const BitBoard &bb, const std::array<int, BOARD_WIDTH> &h)
{
    int cavities = 0;
    int overhangs = 0;
    int max_h = 0;
    for (int hh : h)
        if (hh > max_h)
            max_h = hh;

    for (int y = 0; y < max_h; ++y)
    {
        for (int x = 0; x < BOARD_WIDTH; ++x)
        {
            if (occupied(bb, x, y) || y >= h[x])
                continue;

            bool is_overhang = false;
            if (x > 1 && h[x - 1] <= y - 1 && h[x - 2] <= y)
                is_overhang = true;
            if (!is_overhang && x < 8 && h[x + 1] <= y - 1 && h[x + 2] <= y)
                is_overhang = true;

            if (is_overhang)
                overhangs++;
            else
                cavities++;
        }
    }
    return {cavities, overhangs};
}

static std::pair<int, int> covered_cells(const BitBoard &bb, const std::array<int, BOARD_WIDTH> &h)
{
    int covered = 0;
    int covered_sq = 0;
    for (int x = 0; x < BOARD_WIDTH; ++x)
    {
        for (int y = h[x] - 2; y >= 0; --y)
        {
            if (!occupied(bb, x, y))
            {
                int cells = std::min(6, h[x] - y - 1);
                covered += cells;
                covered_sq += cells * cells;
            }
        }
    }
    return {covered, covered_sq};
}

// ----------------------------------------------------------------------
// Evaluate Board (Fully Updated)
// ----------------------------------------------------------------------

int evaluate_board(const GameState &gs, const EvalWeights &w)
{
    // Use local copy for recursive simulation
    BitBoard current_bb = gs.bb;
    auto h = get_heights(current_bb);

    int score = 0;

    // ---------------------------------------------------------
    // 1. T-Count 計算 (Bag + Hold)
    // ---------------------------------------------------------
    int t_count = 0;

    // Holdにあるか？
    if (gs.has_hold && gs.hold_piece == PieceType::T)
    {
        t_count++;
    }

    // Bagにあるか？
    if (gs.bag)
    {
        // 【Rootノードの場合】
        // 本物のBag配列を見る。直近7手以内にTがあるか？
        for (size_t i = 0; i < 7 && (size_t)(gs.bag_index + i) < gs.bag->size(); ++i)
        {
            if (gs.bag->at(gs.bag_index + i) == PieceType::T)
            {
                t_count++;
                break; // 通常、評価に使う未来のTは1個先までで十分
            }
        }
    }
    else
    {
        // 【探索ノードの場合】
        // ポインタがないので mask を見る
        // PieceType::T を int にキャストしてビットチェック
        if ((gs.bag_mask >> (int)PieceType::T) & 1)
        {
            t_count++;
        }
    }
    // 2. Recursive T-Slot Cutout
    // Attempt to "use up" the available T pieces on existing slots
    for (int i = 0; i < t_count; ++i)
    {
        TSlotMatch match = {0, 0, 0, false};

        // Priority: Sky > Twist > Cave > Fin
        if (!match.found)
            match = sky_tslot_left(current_bb, h);
        if (!match.found)
            match = sky_tslot_right(current_bb, h);

        // Twist & Cave logic
        if (!match.found)
        {
            TSlotMatch tst = {0, 0, 0, false};
            tst = tst_twist_left(current_bb, h);
            if (!tst.found)
                tst = tst_twist_right(current_bb, h);

            if (tst.found)
            {
                // If TST found, check if it can be a Cave
                TSlotMatch cave = cave_tslot(current_bb, tst);
                if (cave.found)
                {
                    // Check corners for Cave validity
                    // 3 corners rule check
                    int cx = cave.x, cy = cave.y;
                    int corners = occupied(current_bb, cx - 1, cy - 1) + occupied(current_bb, cx + 1, cy - 1) +
                                  occupied(current_bb, cx - 1, cy + 1) + occupied(current_bb, cx + 1, cy + 1);
                    if (corners >= 3)
                        match = cave;
                }

                // If not cave, use TST (but TST usually needs to be cleared, so match=tst? CC logic complex here)
                // For now, if cave not found, we don't assume TST is immediately clearable unless stack matches
                // Simplification: If cave not found, do not use TST directly here as T-Spin Single/Double
            }
        }

        if (!match.found)
            match = fin_left(current_bb, h);
        if (!match.found)
            match = fin_right(current_bb, h);

        if (match.found)
        {
            auto res = cutout_tslot(current_bb, match.x, match.y, match.r);
            if (res.success)
            {
                if (res.lines >= 0 && res.lines <= 3)
                {
                    score += w.tslot[res.lines];
                }
                current_bb = res.bb;
                h = get_heights(current_bb); // Re-calculate heights
            }
            else
            {
                break; // Failed to cut, stop recursion
            }
        }
        else
        {
            break; // No slot found
        }
    }

    // 3. Basic Features Calculation
    int maxh = 0;
    for (int hh : h)
        if (hh > maxh)
            maxh = hh;

    score += w.height * maxh;
    score += w.top_quarter * std::max(0, maxh - 15);
    score += w.top_half * std::max(0, maxh - 10);

    // Jeopardy: Time approximation
    // CC uses actual move time, here we assume average case (10ms)
    int move_time_approx = 10;
    score += w.jeopardy * std::max(0, maxh - 10) * move_time_approx / 10;

    // Well
    int well = 0;
    for (int x = 1; x < BOARD_WIDTH; ++x)
    {
        if (h[x] <= h[well])
            well = x;
    }

    int depth = 0;
    for (int y = h[well]; y < TOTAL_BOARD_HEIGHT; ++y)
    {
        bool enclosed = true;
        for (int x = 0; x < BOARD_WIDTH; ++x)
        {
            if (x != well && !occupied(current_bb, x, y))
            {
                enclosed = false;
                break;
            }
        }
        if (!enclosed)
            break;
        depth++;
    }
    depth = std::min(depth, w.max_well_cap);
    score += w.well_depth * depth;
    if (depth > 0)
        score += w.well_column[well];

    if (w.row_trans != 0)
        score += w.row_trans * row_transitions(current_bb);

    if (w.bumpiness != 0 || w.bumpiness_sq != 0)
    {
        auto [b, b_sq] = bumpiness(h, well);
        score += w.bumpiness * b;
        score += w.bumpiness_sq * b_sq;
    }

    if (w.cavity_cells != 0 || w.overhang_cells != 0)
    {
        auto [cav, over] = cavities_and_overhangs(current_bb, h);
        score += w.cavity_cells * cav;
        score += w.cavity_cells_sq * cav * cav;
        score += w.overhang_cells * over;
        score += w.overhang_cells_sq * over * over;
    }

    if (w.covered != 0 || w.covered_sq != 0)
    {
        auto [cov, cov_sq] = covered_cells(current_bb, h);
        score += w.covered * cov;
        score += w.covered_sq * cov_sq;
    }

    return score;
}

// ----------------------------------------------------------------------
// Evaluate Landing (Reward) - Unchanged largely but tuned
// ----------------------------------------------------------------------

int evaluate_landing(const Landing &l, const EvalWeights &w)
{
    int score = 0;

    if (l.perfect_clear)
        score += w.perfect_clear;

    bool is_b2b_action =
        l.kind == ClearKind::Tspin1 || l.kind == ClearKind::Tspin2 || l.kind == ClearKind::Tspin3 ||
        l.kind == ClearKind::MiniTspin1 || l.kind == ClearKind::MiniTspin2 ||
        l.kind == ClearKind::Clear4;

    if (l.back_to_back && is_b2b_action)
    {
        score += w.b2b_clear;
    }

    if (l.lines_cleared > 0 && l.combo > 0)
    {
        static const int GARBAGE_TABLE[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5, 5};
        int idx = std::min(l.combo, 14);
        score += w.combo_bonus * GARBAGE_TABLE[idx];
    }

    switch (l.kind)
    {
    case ClearKind::Clear1:
        score += w.clear1;
        break;
    case ClearKind::Clear2:
        score += w.clear2;
        break;
    case ClearKind::Clear3:
        score += w.clear3;
        break;
    case ClearKind::Clear4:
        score += w.clear4;
        break;
    case ClearKind::Tspin1:
        score += w.tspin1;
        break;
    case ClearKind::Tspin2:
        score += w.tspin2;
        break;
    case ClearKind::Tspin3:
        score += w.tspin3;
        break;
    case ClearKind::MiniTspin1:
        score += w.mini_tspin1;
        break;
    case ClearKind::MiniTspin2:
        score += w.mini_tspin2;
        break;
    default:
        break;
    }

    if (l.used_t_piece && !is_b2b_action)
    {
        score += w.wasted_t;
    }

    int time = 10;
    if (l.lines_cleared > 0)
        time += 40;
    score += w.move_time * time;

    return score;
}