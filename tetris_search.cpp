#include "tetris_search.h"
#include "tetris_step.h"
#include "tetris_attack.h"
#include "tetris_state.h"

#include <atomic>
#include <chrono>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <array>

// ============================
// 計測（任意）
// ============================
static std::atomic<long long> g_enum_calls{0};
static std::atomic<long long> g_enum_ns{0};

void print_enum_stats()
{
    long long calls = g_enum_calls.load(std::memory_order_relaxed);
    long long ns = g_enum_ns.load(std::memory_order_relaxed);
    if (calls == 0)
        return;

    double total_ms = (double)ns / 1e6;
    double avg_ms = total_ms / (double)calls;

    std::cerr
        << "[enumerate_landings]\n"
        << "  calls    = " << calls << "\n"
        << "  total_ms = " << total_ms << " ms\n"
        << "  avg_ms   = " << avg_ms << " ms\n";
}

// ============================
// 探索範囲（少し広め）
// ============================
static constexpr int X_MIN = -4;
static constexpr int X_MAX = BOARD_WIDTH + 4;
static constexpr int Y_MIN = 0;
static constexpr int Y_MAX = TOTAL_BOARD_HEIGHT + 4;

static constexpr int XN = (X_MAX - X_MIN + 1);
static constexpr int YN = (Y_MAX - Y_MIN + 1);
static constexpr int RN = 4;
static constexpr int MAX_NODES = XN * YN * RN;

static inline bool in_range(int x, int y)
{
    return x >= X_MIN && x <= X_MAX && y >= Y_MIN && y <= Y_MAX;
}

static inline int node_index(int x, int y, int r)
{
    return ((y - Y_MIN) * XN + (x - X_MIN)) * RN + (r & 3);
}

static inline void decode(int idx, int &x, int &y, int &r)
{
    r = idx % RN;
    idx /= RN;
    x = idx % XN + X_MIN;
    y = idx / XN + Y_MIN;
}

inline int compute_attack_search(const LandingSearch &l)
{
    Landing tmp;
    tmp.lines_cleared = l.lines_cleared;
    tmp.kind = l.kind;
    tmp.combo = l.combo_after;
    tmp.back_to_back = l.b2b_after;
    return compute_attack(tmp);
}

// ============================
// 静的バッファ（探索状態）
// ============================
static int visit_gen[MAX_NODES];
static int dista[MAX_NODES];
static int parent_[MAX_NODES];
static Action pact_[MAX_NODES];
static int tail_soft[MAX_NODES];
static int cur_gen = 0;


// ============================
// Phase1: landing候補の収集（全スキャン排除）
// ============================
static int landing_gen[MAX_NODES]; // idx がこの世代で landing 登録されたか
static int best_clen[MAX_NODES];   // best compressed length
static int best_tail[MAX_NODES];   // tie-break: tail_soft を優先
static uint8_t last_is_rot[MAX_NODES];
static uint8_t last_rot_dir[MAX_NODES];
static int active_landings[512];   // landing idx リスト
static int active_cnt = 0;

// ============================
// ヘルパー
// ============================
static inline bool is_rot(Action a)
{
    return a == Action::RotateCW || a == Action::RotateCCW;
}

static inline bool is_inverse_action(Action prev, Action curr)
{
    if (prev == Action::MoveLeft && curr == Action::MoveRight)
        return true;
    if (prev == Action::MoveRight && curr == Action::MoveLeft)
        return true;
    if (prev == Action::RotateCW && curr == Action::RotateCCW)
        return true;
    if (prev == Action::RotateCCW && curr == Action::RotateCW)
        return true;
    return false;
}

// compressed length（従来の tie-break を維持）
static inline int compressed_cost(int d, int tail)
{
    return (tail >= 2) ? d - (tail - 1) : d;
}

// 「末尾SoftDrop連打」を圧縮して HardDrop に置き換え
void compress_softdrop(std::vector<Action> &p)
{
    int k = 0;
    for (int i = (int)p.size() - 1; i >= 0 && p[i] == Action::SoftDrop; --i)
        k++;

    if (k >= 2)
    {
        p.erase(p.end() - k, p.end());
        p.push_back(Action::HardDrop);
    }
}

// ============================
// ClearKind 判定（あなたの judge_clear_kind を関数化）
//  - last_rot: 「最後の non-drop が rotation」
// ============================
ClearKind judge_clear_kind(
    const BitBoard &bb_before,
    PieceType piece,
    int final_x,
    int final_y,
    int final_rot,
    int lines_cleared,
    bool last_rot)
{
    if (lines_cleared == 0)
        return ClearKind::None;

    auto inside = [](int x, int y)
    {
        return (0 <= x && x < BOARD_WIDTH &&
                0 <= y && y < TOTAL_BOARD_HEIGHT);
    };

    if (piece == PieceType::T && last_rot)
    {
        const int cx = final_x;
        const int cy = final_y;

        static const std::array<std::pair<int, int>, 4> corners =
            {{{-1, -1}, {+1, -1}, {-1, +1}, {+1, +1}}};

        int corner_filled = 0;
        for (auto [dx, dy] : corners)
        {
            int x = cx + dx;
            int y = cy + dy;
            if (!inside(x, y) || (bb_before.row[y] & (1u << x)))
                corner_filled++;
        }

        if (corner_filled >= 3)
        {
            static const std::array<std::array<std::pair<int, int>, 2>, 4> front = {{
                {{{-1, +1}, {+1, +1}}}, // 0
                {{{-1, -1}, {-1, +1}}}, // R
                {{{-1, -1}, {+1, -1}}}, // 2
                {{{+1, -1}, {+1, +1}}}, // L
            }};

            int front_filled = 0;
            for (auto [dx, dy] : front[final_rot & 3])
            {
                int x = cx + dx;
                int y = cy + dy;
                if (!inside(x, y) || (bb_before.row[y] & (1u << x)))
                    front_filled++;
            }

            bool is_mini = (front_filled < 2);
            if (is_mini)
            {
                if (lines_cleared == 1)
                    return ClearKind::MiniTspin1;
                if (lines_cleared == 2)
                    return ClearKind::MiniTspin2;
            }
            else
            {
                if (lines_cleared == 1)
                    return ClearKind::Tspin1;
                if (lines_cleared == 2)
                    return ClearKind::Tspin2;
                if (lines_cleared == 3)
                    return ClearKind::Tspin3;
            }
        }
    }

    if (lines_cleared == 4)
        return ClearKind::Clear4;
    if (lines_cleared == 3)
        return ClearKind::Clear3;
    if (lines_cleared == 2)
        return ClearKind::Clear2;
    if (lines_cleared == 1)
        return ClearKind::Clear1;
    return ClearKind::None;
}

inline void propagate_spin(
    int cur,
    Action a,
    uint8_t &out_last_is_rot,
    uint8_t &out_last_rot_dir)
{
    if (a == Action::RotateCW)
    {
        out_last_is_rot = 1;
        out_last_rot_dir = 1;
    }
    else if (a == Action::RotateCCW)
    {
        out_last_is_rot = 1;
        out_last_rot_dir = 2;
    }
    else if (a == Action::MoveLeft || a == Action::MoveRight)
    {
        out_last_is_rot = 0;
        out_last_rot_dir = 0;
    }
    else
    {
        // SoftDrop → non-drop → 維持
        out_last_is_rot = last_is_rot[cur];
        out_last_rot_dir = last_rot_dir[cur];
    }
}



// ============================
// enumerate_landings (Phase1 正しい形)
// ============================
static std::vector<LandingSearch>
enumerate_place_only(const GameState &s)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    g_enum_calls++;

    const BitBoard &bb = s.bb;
    const PieceType piece = s.current;

    int spawn_x = s.spawn_x;
    int spawn_y = s.spawn_y;

    std::vector<LandingSearch> out;
    out.reserve(64);

    // ============================
    // generation
    // ============================
    ++cur_gen;
    if (cur_gen <= 0)
    {
        std::memset(visit_gen, 0, sizeof(visit_gen));
        std::memset(landing_gen, 0, sizeof(landing_gen));
        cur_gen = 1;
    }

    // ============================
    // spawn check
    // ============================
    if (!is_valid_position(bb, piece, 0, spawn_x, spawn_y))
    {
        if (is_valid_position(bb, piece, 0, spawn_x, spawn_y + 1))
            ++spawn_y;
        else
            return out;
    }
    if (!in_range(spawn_x, spawn_y))
        return out;

    // ============================
    // costs
    // ============================
    static constexpr int COST_MOVE = 10;
    static constexpr int COST_ROT = 10;
    static constexpr int COST_SOFT = 11;

    // ============================
    // Dial buckets
    // ============================
    static constexpr int BUCKET_SIZE = 64;
    static constexpr int BUCKET_MASK = BUCKET_SIZE - 1;
    static constexpr int BUCKET_CAP = 512;

    struct Bucket
    {
        int data[BUCKET_CAP];
        int size;
        inline void clear() { size = 0; }
        inline bool empty() const { return size == 0; }
        inline void push(int v)
        {
            if (size < BUCKET_CAP)
                data[size++] = v;
        }
        inline int pop() { return data[--size]; }
    };

    static Bucket buckets[BUCKET_SIZE];
    for (auto &b : buckets)
        b.clear();

    active_cnt = 0;

    // ============================
    // init node
    // ============================
    const int start = node_index(spawn_x, spawn_y, 0);
    visit_gen[start] = cur_gen;
    dista[start] = 0;
    tail_soft[start] = 0;
    last_is_rot[start] = 0;
    parent_[start] = -1;
    pact_[start] = Action::None;

    buckets[0].push(start);
    int queued = 1;
    int current_cost = 0;

    // ============================
    // relax helpers
    // ============================
    auto relax_valid = [&](int cur, int ni, int nd, int nt, Action a)
    {
        if (parent_[cur] != -1 && is_inverse_action(pact_[cur], a))
            return;

        if (visit_gen[ni] != cur_gen || nd < dista[ni])
        {
            visit_gen[ni] = cur_gen;
            dista[ni] = nd;
            parent_[ni] = cur;
            pact_[ni] = a;
            tail_soft[ni] = nt;

            if (a == Action::RotateCW || a == Action::RotateCCW)
                last_is_rot[ni] = 1;
            else if (a == Action::MoveLeft || a == Action::MoveRight)
                last_is_rot[ni] = 0;
            else
                last_is_rot[ni] = last_is_rot[cur];

            buckets[nd & BUCKET_MASK].push(ni);
            ++queued;
        }
    };

    auto try_move = [&](int cur, int cx, int cy, int cr, int nx, int ny, Action a)
    {
        if (!is_valid_position(bb, piece, cr, nx, ny))
            return;
        relax_valid(cur, node_index(nx, ny, cr),
                    dista[cur] + COST_MOVE, 0, a);
    };

    auto try_drop = [&](int cur, int cx, int cy, int cr)
    {
        int ny = cy - 1;
        if (!is_valid_position(bb, piece, cr, cx, ny))
            return;
        relax_valid(cur, node_index(cx, ny, cr),
                    dista[cur] + COST_SOFT, tail_soft[cur] + 1,
                    Action::SoftDrop);
    };

    auto try_rot = [&](int cur, int cx, int cy, int cr, int dir, Action a)
    {
        const int nr = (cr + dir) & 3;
        const KickList &kl = get_kick(piece, cr, nr);

        int nd = dista[cur] + COST_ROT;
        if (cy < 8)
            nd += 10;

        for (int i = 0; i < kl.n; ++i)
        {
            int nx = cx + kl.dx[i];
            int ny = cy + kl.dy[i];
            if (!is_valid_position(bb, piece, nr, nx, ny))
                continue;
            relax_valid(cur, node_index(nx, ny, nr), nd, 0, a);
            return;
        }
    };

    // ============================
    // search loop
    // ============================
    while (queued > 0)
    {
        while (buckets[current_cost & BUCKET_MASK].empty())
            ++current_cost;

        int cur = buckets[current_cost & BUCKET_MASK].pop();
        --queued;

        if (dista[cur] != current_cost)
            continue;

        int cx, cy, cr;
        decode(cur, cx, cy, cr);

        if (!is_valid_position(bb, piece, cr, cx, cy - 1))
        {
            if (landing_gen[cur] != cur_gen && active_cnt < 512)
            {
                landing_gen[cur] = cur_gen;
                active_landings[active_cnt++] = cur;
            }
        }

        try_move(cur, cx, cy, cr, cx - 1, cy, Action::MoveLeft);
        try_move(cur, cx, cy, cr, cx + 1, cy, Action::MoveRight);
        try_rot(cur, cx, cy, cr, +1, Action::RotateCW);
        try_rot(cur, cx, cy, cr, -1, Action::RotateCCW);
        try_drop(cur, cx, cy, cr);
    }

    // ============================
    // finalize landings
    // ============================
    for (int i = 0; i < active_cnt; ++i)
    {
        int idx = active_landings[i];
        int fx, fy, fr;
        decode(idx, fx, fy, fr);

        StepResult r = step_lock_piece_at(bb, piece, fr, fx, fy);

        LandingSearch l{};
        l.piece = piece;
        l.final_x = fx;
        l.final_y = fy;
        l.final_rot = fr;

        // ★ 正しい盤面
        l.bb_after = r.bb_after;
        l.lines_cleared = r.lines_cleared;
        l.perfect_clear = r.perfect_clear;

        // combo / b2b AFTER
        l.combo_after = (r.lines_cleared > 0 ? s.combo + 1 : 0);


        l.kind = judge_clear_kind(
            bb, piece, fx, fy, fr,
            r.lines_cleared,
            last_is_rot[idx]);

        bool is_b2b_action =
            l.kind == ClearKind::Clear4 ||
            l.kind == ClearKind::Tspin1 ||
            l.kind == ClearKind::Tspin2 ||
            l.kind == ClearKind::Tspin3 ||
            l.kind == ClearKind::MiniTspin1 ||
            l.kind == ClearKind::MiniTspin2;

        l.b2b_after =
            is_b2b_action ? true : (r.lines_cleared > 0 ? false : s.back_to_back);

        // attack
        {
            Landing tmp{};
            tmp.lines_cleared = l.lines_cleared;
            tmp.kind = l.kind;
            tmp.combo = l.combo_after;
            tmp.back_to_back = l.b2b_after;
            l.attack = compute_attack(tmp);
        }

        // ---------- AFTER STATE ----------
        l.has_hold_after = s.has_hold;
        l.hold_piece_after = s.hold_piece;

        l.current_after = s.bag->at(s.bag_index);
        l.bag_index_after = s.bag_index + 1;

        auto [sx, sy] = spawn_position_with_fallback(l.bb_after, l.current_after);
        l.spawn_x_after = sx;
        l.spawn_y_after = sy;
        l.dead_after = (sx < 0);

        l.used_hold_this_turn_after = false;
        l.node_idx = idx;

        out.push_back(l);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    g_enum_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    return out;
}

// ============================================================
// enumerate_landings_positions_virtual
//  - DAG / Search 用の「純粋な着地点列挙」
//  - bag / hold / next / spawn_after には一切触らない
//  - return: (final_x, final_y, final_rot, last_is_rot)
// ============================================================

std::vector<std::tuple<int, int, int, bool>>
enumerate_landings_positions_virtual(
    const BitBoard &bb,
    PieceType piece,
    int spawn_x,
    int spawn_y)
{
    std::vector<std::tuple<int, int, int, bool>> out;
    out.reserve(64);

    // ============================
    // generation (そのまま流用)
    // ============================
    ++cur_gen;
    if (cur_gen <= 0)
    {
        std::memset(visit_gen, 0, sizeof(visit_gen));
        std::memset(landing_gen, 0, sizeof(landing_gen));
        cur_gen = 1;
    }

    // ============================
    // spawn check
    // ============================
    if (!is_valid_position(bb, piece, 0, spawn_x, spawn_y))
    {
        if (is_valid_position(bb, piece, 0, spawn_x, spawn_y + 1))
            ++spawn_y;
        else
            return out;
    }
    if (!in_range(spawn_x, spawn_y))
        return out;

    // ============================
    // costs
    // ============================
    static constexpr int COST_MOVE = 10;
    static constexpr int COST_ROT = 10;
    static constexpr int COST_SOFT = 11;

    // ============================
    // Dial buckets
    // ============================
    static constexpr int BUCKET_SIZE = 64;
    static constexpr int BUCKET_MASK = BUCKET_SIZE - 1;
    static constexpr int BUCKET_CAP = 512;

    struct Bucket
    {
        int data[BUCKET_CAP];
        int size;
        inline void clear() { size = 0; }
        inline bool empty() const { return size == 0; }
        inline void push(int v)
        {
            if (size < BUCKET_CAP)
                data[size++] = v;
        }
        inline int pop() { return data[--size]; }
    };

    static Bucket buckets[BUCKET_SIZE];
    for (auto &b : buckets)
        b.clear();

    active_cnt = 0;

    // ============================
    // init node
    // ============================
    const int start = node_index(spawn_x, spawn_y, 0);
    visit_gen[start] = cur_gen;
    dista[start] = 0;
    tail_soft[start] = 0;
    last_is_rot[start] = 0;
    parent_[start] = -1;
    pact_[start] = Action::None;

    buckets[0].push(start);
    int queued = 1;
    int current_cost = 0;

    // ============================
    // relax helpers
    // ============================
    auto relax_valid = [&](int cur, int ni, int nd, int nt, Action a)
    {
        if (parent_[cur] != -1 && is_inverse_action(pact_[cur], a))
            return;

        if (visit_gen[ni] != cur_gen || nd < dista[ni])
        {
            visit_gen[ni] = cur_gen;
            dista[ni] = nd;
            parent_[ni] = cur;
            pact_[ni] = a;
            tail_soft[ni] = nt;

            if (a == Action::RotateCW || a == Action::RotateCCW)
                last_is_rot[ni] = 1;
            else if (a == Action::MoveLeft || a == Action::MoveRight)
                last_is_rot[ni] = 0;
            else
                last_is_rot[ni] = last_is_rot[cur];

            buckets[nd & BUCKET_MASK].push(ni);
            ++queued;
        }
    };

    auto try_move = [&](int cur, int cx, int cy, int cr, int nx, int ny, Action a)
    {
        if (!is_valid_position(bb, piece, cr, nx, ny))
            return;
        relax_valid(cur, node_index(nx, ny, cr),
                    dista[cur] + COST_MOVE, 0, a);
    };

    auto try_drop = [&](int cur, int cx, int cy, int cr)
    {
        int ny = cy - 1;
        if (!is_valid_position(bb, piece, cr, cx, ny))
            return;
        relax_valid(cur, node_index(cx, ny, cr),
                    dista[cur] + COST_SOFT, tail_soft[cur] + 1,
                    Action::SoftDrop);
    };

    auto try_rot = [&](int cur, int cx, int cy, int cr, int dir, Action a)
    {
        const int nr = (cr + dir) & 3;
        const KickList &kl = get_kick(piece, cr, nr);

        int nd = dista[cur] + COST_ROT;
        if (cy < 8)
            nd += 10;

        for (int i = 0; i < kl.n; ++i)
        {
            int nx = cx + kl.dx[i];
            int ny = cy + kl.dy[i];
            if (!is_valid_position(bb, piece, nr, nx, ny))
                continue;
            relax_valid(cur, node_index(nx, ny, nr), nd, 0, a);
            return;
        }
    };

    // ============================
    // search loop
    // ============================
    while (queued > 0)
    {
        while (buckets[current_cost & BUCKET_MASK].empty())
            ++current_cost;

        int cur = buckets[current_cost & BUCKET_MASK].pop();
        --queued;

        if (dista[cur] != current_cost)
            continue;

        int cx, cy, cr;
        decode(cur, cx, cy, cr);

        // ---- landing check ----
        if (!is_valid_position(bb, piece, cr, cx, cy - 1))
        {
            if (landing_gen[cur] != cur_gen && active_cnt < 512)
            {
                landing_gen[cur] = cur_gen;
                active_landings[active_cnt++] = cur;
            }
        }

        try_move(cur, cx, cy, cr, cx - 1, cy, Action::MoveLeft);
        try_move(cur, cx, cy, cr, cx + 1, cy, Action::MoveRight);
        try_rot(cur, cx, cy, cr, +1, Action::RotateCW);
        try_rot(cur, cx, cy, cr, -1, Action::RotateCCW);
        try_drop(cur, cx, cy, cr);
    }

    // ============================
    // finalize landings (NO after-state)
    // ============================
    for (int i = 0; i < active_cnt; ++i)
    {
        int idx = active_landings[i];
        int fx, fy, fr;
        decode(idx, fx, fy, fr);

        bool last_rot = (last_is_rot[idx] != 0);
        out.emplace_back(fx, fy, fr, last_rot);
    }

    return out;
}

std::vector<LandingSearch>
enumerate_landings_search(const GameState &s)
{
    std::vector<LandingSearch> out;

    // =====================
    // 1) no-hold
    // =====================
    {
        auto v = enumerate_place_only(s);
        for (auto &l : v)
        {
            l.used_hold = false;
            out.push_back(l);
        }
    }

    // =====================
    // 2) hold
    // =====================
    if (!s.used_hold_this_turn)
    {
        GameState hs = s;
        hs.used_hold_this_turn = true;

        if (!s.has_hold)
        {
            // ---- 空hold ----
            hs.hold_piece = s.current;
            hs.has_hold = true;

            hs.current = s.bag->at(s.bag_index);
            hs.bag_index++;
        }
        else
        {
            // ---- swap ----
            std::swap(hs.current, hs.hold_piece);
        }

        auto [sx, sy] = spawn_position_with_fallback(hs.bb, hs.current);
        hs.spawn_x = sx;
        hs.spawn_y = sy;
        hs.dead = (sx < 0);

        if (!hs.dead)
        {
            auto v = enumerate_place_only(hs);
            for (auto &l : v)
            {
                l.used_hold = true;
                l.has_hold_after = hs.has_hold;
                l.hold_piece_after = hs.hold_piece;
                l.used_hold_this_turn_after = true;
                out.push_back(l);
            }
        }
    }

    return out;
}

std::vector<Landing>
enumerate_landings(
    const BitBoard &bb,
    PieceType piece,
    int spawn_x,
    int spawn_y,
    int current_combo,
    bool current_b2b)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    g_enum_calls++;

    std::vector<Landing> out;
    out.reserve(64);

    // =====================================================
    // generation
    // =====================================================
    ++cur_gen;
    if (cur_gen <= 0)
    {
        std::memset(visit_gen, 0, sizeof(visit_gen));
        std::memset(landing_gen, 0, sizeof(landing_gen));
        cur_gen = 1;
    }

    // =====================================================
    // spawn check
    // =====================================================
    if (!is_valid_position(bb, piece, 0, spawn_x, spawn_y))
    {
        if (is_valid_position(bb, piece, 0, spawn_x, spawn_y + 1))
            ++spawn_y;
        else
            return {};
    }
    if (!in_range(spawn_x, spawn_y))
        return {};

    // =====================================================
    // cost
    // =====================================================
    static constexpr int COST_MOVE = 10;
    static constexpr int COST_ROT = 10;
    static constexpr int COST_SOFT = 11;

    // =====================================================
    // Dial BFS buckets
    // =====================================================
    static constexpr int BUCKET_SIZE = 64;
    static constexpr int BUCKET_MASK = BUCKET_SIZE - 1;
    static constexpr int BUCKET_CAP = 256;

    struct Bucket
    {
        int data[BUCKET_CAP];
        int size;
        inline void clear() { size = 0; }
        inline bool empty() const { return size == 0; }
        inline void push(int v) { data[size++] = v; }
        inline int pop() { return data[--size]; }
    };

    static Bucket buckets[BUCKET_SIZE];
    for (auto &b : buckets)
        b.clear();

    active_cnt = 0;

    // =====================================================
    // init node
    // =====================================================
    const int start = node_index(spawn_x, spawn_y, 0);
    visit_gen[start] = cur_gen;
    dista[start] = 0;
    parent_[start] = -1;
    pact_[start] = Action::None;
    tail_soft[start] = 0;

    buckets[0].push(start);
    int queued = 1;
    int current_cost = 0;

    // =====================================================
    // relax helper
    // =====================================================
    auto relax = [&](int cur, int ni, int nd, int nt, Action a)
    {
        if (parent_[cur] != -1 && is_inverse_action(pact_[cur], a))
            return;

        if (visit_gen[ni] != cur_gen || nd < dista[ni])
        {
            visit_gen[ni] = cur_gen;
            dista[ni] = nd;
            parent_[ni] = cur;
            pact_[ni] = a;
            tail_soft[ni] = nt;

            buckets[nd & BUCKET_MASK].push(ni);
            ++queued;
        }
    };

    // =====================================================
    // search loop
    // =====================================================
    while (queued > 0)
    {
        while (buckets[current_cost & BUCKET_MASK].empty())
            ++current_cost;

        Bucket &b = buckets[current_cost & BUCKET_MASK];
        const int cur = b.pop();
        --queued;

        if (dista[cur] != current_cost)
            continue;

        int cx, cy, cr;
        decode(cur, cx, cy, cr);

        // ----------------------------
        // landing check
        // ----------------------------
        if (!is_valid_position(bb, piece, cr, cx, cy - 1))
        {
            const int clen = compressed_cost(dista[cur], tail_soft[cur]);
            const int tl = tail_soft[cur];

            if (landing_gen[cur] != cur_gen)
            {
                landing_gen[cur] = cur_gen;
                best_clen[cur] = clen;
                best_tail[cur] = tl;
                if (active_cnt < 512)
                    active_landings[active_cnt++] = cur;
            }
            else if (clen < best_clen[cur] ||
                     (clen == best_clen[cur] && tl > best_tail[cur]))
            {
                best_clen[cur] = clen;
                best_tail[cur] = tl;
            }
        }

        // ----------------------------
        // move
        // ----------------------------
        auto try_move = [&](int nx, int ny, Action a)
        {
            if (!in_range(nx, ny))
                return;
            if (!is_valid_position(bb, piece, cr, nx, ny))
                return;

            relax(cur,
                  node_index(nx, ny, cr),
                  dista[cur] + COST_MOVE,
                  0,
                  a);
        };

        try_move(cx - 1, cy, Action::MoveLeft);
        try_move(cx + 1, cy, Action::MoveRight);

        // ----------------------------
        // rotate (SRS: first success only)
        // ----------------------------
        auto try_rot = [&](int dir, Action a)
        {
            int nr = (cr + dir) & 3;
            const KickList &kl = get_kick(piece, cr, nr);

            int nd = dista[cur] + COST_ROT;
            if (cy < 8)
                nd += 10;

            for (int i = 0; i < kl.n; ++i)
            {
                int nx = cx + kl.dx[i];
                int ny = cy + kl.dy[i];
                if (!in_range(nx, ny))
                    continue;
                if (!is_valid_position(bb, piece, nr, nx, ny))
                    continue;

                relax(cur,
                      node_index(nx, ny, nr),
                      nd,
                      0,
                      a);
                return;
            }
        };

        try_rot(+1, Action::RotateCW);
        try_rot(-1, Action::RotateCCW);

        // ----------------------------
        // soft drop
        // ----------------------------
        int ny = cy - 1;
        if (in_range(cx, ny) &&
            is_valid_position(bb, piece, cr, cx, ny))
        {
            relax(cur,
                  node_index(cx, ny, cr),
                  dista[cur] + COST_SOFT,
                  tail_soft[cur] + 1,
                  Action::SoftDrop);
        }
    }

    // =====================================================
    // reconstruct (full Landing)
    // =====================================================
    for (int i = 0; i < active_cnt; ++i)
    {
        int idx = active_landings[i];
        if (landing_gen[idx] != cur_gen)
            continue;

        int fx, fy, fr;
        decode(idx, fx, fy, fr);

        // path restore
        std::vector<Action> path;
        for (int p = idx; parent_[p] != -1; p = parent_[p])
            path.push_back(pact_[p]);
        std::reverse(path.begin(), path.end());
        compress_softdrop(path);

        StepResult r = step_lock_piece_at(bb, piece, fr, fx, fy);

        bool last_rot = false;
        for (int k = (int)path.size() - 1; k >= 0; --k)
        {
            if (path[k] == Action::SoftDrop || path[k] == Action::HardDrop)
                continue;
            last_rot = (path[k] == Action::RotateCW ||
                        path[k] == Action::RotateCCW);
            break;
        }

        Landing l;
        l.piece = piece;
        l.final_x = fx;
        l.final_y = fy;
        l.final_rot = fr;
        l.path = std::move(path);
        l.bb_after = r.bb_after;
        l.lines_cleared = r.lines_cleared;
        l.perfect_clear = r.perfect_clear;
        l.combo = (current_combo >= 0 && r.lines_cleared > 0)
                      ? current_combo + 1
                      : 0;
        l.back_to_back = current_b2b;
        l.kind = judge_clear_kind(bb, piece, fx, fy, fr,
                                  r.lines_cleared, last_rot);
        l.attack = compute_attack(l);

        out.push_back(std::move(l));
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    g_enum_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    return out;
}
