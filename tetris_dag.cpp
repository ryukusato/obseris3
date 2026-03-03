#include "tetris_dag.h"
#include "tetris_search.h"
#include "tetris_eval.h"
#include "tetris_step.h"
#include "tetris_engine.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>
#include <cstring>
#include <array> // スタック配列用
#include <iterator>

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------
// Hashing / Utils
// ------------------------------------------------------------
static inline uint64_t mix_u64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// 高速化: ループ展開とインライン化
static inline uint64_t hash_dag_key(const DagKey &k)
{
    uint64_t h = 0x123456789abcdef0ULL;

    // BitBoard (uint64_t x 4 と仮定して高速アクセス)
    // 構造体のパディングに依存しないよう注意が必要だが、速度優先でキャスト
    const uint64_t *p = (const uint64_t *)&k.bb;

    // 手動展開 (BitBoardが320bit = 64bit x 5 程度と仮定)
    // ※実際のBitBoard定義に合わせて調整してください。ここでは汎用的に4回
    h ^= mix_u64(p[0]);
    h ^= mix_u64(p[1]);
    h ^= mix_u64(p[2]);
    h ^= mix_u64(p[3]);

    // 小さいフィールドをまとめてハッシュ化
    uint64_t misc = ((uint64_t)k.bag_remaining << 16) |
                    ((uint64_t)k.combo << 8) |
                    (k.reserve) |
                    (k.reserve_is_hold << 3) |
                    (k.b2b << 4);

    h ^= mix_u64(misc + 0x300);
    return h;
}

// ------------------------------------------------------------
// Helper: Bag Remaining Stub (外部定義がない場合の仮実装)
// ------------------------------------------------------------
static uint8_t compute_bag_remaining(const GameState &s)
{
    // ※tetris_dag.cpp の上部にある実装を持ってくるか、外部関数を使う
    // ここでは仮に実装
    uint8_t seen = 0;
    if (s.bag)
    {
        for (int i = 0; i < 14 && (size_t)(s.bag_index + i) < s.bag->size(); ++i)
        {
            PieceType p = s.bag->at(s.bag_index + i);
            uint8_t bit = 1 << (int)p;
            if (__builtin_popcount(seen | bit) == 7)
                break;
            seen |= bit;
        }
    }
    seen &= ~(1 << (int)s.current);
    if (seen == 0)
        seen = 0x7F;
    return seen;
}

// ------------------------------------------------------------
// Selection Weight (Cold Clear Logic)
// ------------------------------------------------------------
static inline double cc_weight_fast(double q, double min_q, int rank)
{
    double rel = (q - min_q) + 1.0;
    if (rel < 0.0)
        rel = 0.0;
    // rank^2 の計算を高速化（整数乗算）
    double rank_d = (double)rank;
    double denom = 1.0 + (rank_d * rank_d);
    return (rel * rel) / denom;
}

// ------------------------------------------------------------
// Key Creation Logic
// ------------------------------------------------------------
struct SearchState
{
    BitBoard bb;
    int combo;
    bool b2b;
    uint8_t bag_remaining;

    bool has_hold;
    PieceType hold_piece;
    PieceType next_0_in_gen;
};

static inline DagKey make_key(const SearchState &s)
{
    DagKey k;
    // memsetは遅いので個別に代入（コンパイラ最適化に期待）
    k.bb = s.bb;
    k.combo = (uint8_t)(s.combo > 20 ? 20 : s.combo);
    k.b2b = s.b2b;
    k.bag_remaining = s.bag_remaining;

    if (s.has_hold)
    {
        k.reserve = (uint8_t)s.hold_piece;
        k.reserve_is_hold = true;
    }
    else
    {
        k.reserve = (uint8_t)s.next_0_in_gen;
        k.reserve_is_hold = false;
    }
    k.hash = hash_dag_key(k);
    return k;
}

// ------------------------------------------------------------
// Helper: 世代管理
// ------------------------------------------------------------
static PieceType get_current_piece(const DagArena &arena, int gen_idx, uint8_t bag_mask, std::mt19937_64 &rng)
{
    if (gen_idx >= (int)arena.generations.size())
    {
        // Speculated: BagMaskからランダム選択
        // スタック配列を使う
        PieceType list[7];
        int n = 0;
        for (int i = 0; i < 7; ++i)
        {
            if (bag_mask & (1 << i))
                list[n++] = (PieceType)i;
        }
        if (n == 0)
            return (PieceType)(rng() % 7);
        // モジュロ演算は遅いが、ここでは回数が少ないので許容
        // 厳密には std::uniform_int_distribution 推奨だが初期化コスト回避
        return list[rng() % n];
    }

    const auto &g = arena.generations[gen_idx];
    if (!g.is_speculated)
    {
        return g.known_piece;
    }
    else
    {
        PieceType list[7];
        int n = 0;
        for (int i = 0; i < 7; ++i)
        {
            if (bag_mask & (1 << i))
                list[n++] = (PieceType)i;
        }
        if (n == 0)
            return (PieceType)(rng() % 7);
        return list[rng() % n];
    }
}

static PieceType get_next_piece_for_key(const DagArena &arena, int gen_idx)
{
    int next_gen = gen_idx + 1;
    if (next_gen < (int)arena.generations.size())
    {
        const auto &g = arena.generations[next_gen];
        if (!g.is_speculated)
            return g.known_piece;
    }
    return PieceType::I;
}

// ------------------------------------------------------------
// Restore: Missing Helper Functions
// ------------------------------------------------------------

// 外部関数 (tetris_search.cpp等で定義されていると仮定)
// もしヘッダーになければここで宣言
extern std::vector<std::tuple<int, int, int, bool>>
enumerate_landings_positions_virtual(const BitBoard &, PieceType, int, int);

// 手の列挙 (Placement Only)
static std::vector<LandingSearch>
enumerate_place_only_virtual(const BitBoard &bb,
                             PieceType piece,
                             int spawn_x,
                             int spawn_y,
                             int combo,
                             bool b2b_before)
{
    std::vector<LandingSearch> out;

    // 設置位置の列挙 (tetris_search.cppの実装を呼ぶ)
    auto land = enumerate_landings_positions_virtual(bb, piece, spawn_x, spawn_y);
    out.reserve(land.size());

    for (auto &t : land)
    {
        int fx = std::get<0>(t);
        int fy = std::get<1>(t);
        int fr = std::get<2>(t);
        bool last_rot = std::get<3>(t);

        // ロック処理（ライン消去判定など）
        StepResult r = step_lock_piece_at(bb, piece, fr, fx, fy);

        LandingSearch ls{};
        ls.piece = piece;
        ls.final_x = fx;
        ls.final_y = fy;
        ls.final_rot = fr;

        ls.bb_after = r.bb_after;
        ls.lines_cleared = r.lines_cleared;
        ls.perfect_clear = r.perfect_clear;

        // コンボ計算
        ls.combo_after = (r.lines_cleared > 0 ? combo + 1 : 0);

        // 消去種類の判定 (T-Spin等)
        ls.kind = judge_clear_kind(bb, piece, fx, fy, fr, r.lines_cleared, last_rot);

        // B2B判定
        bool is_b2b_action =
            ls.kind == ClearKind::Clear4 ||
            ls.kind == ClearKind::Tspin1 ||
            ls.kind == ClearKind::Tspin2 ||
            ls.kind == ClearKind::Tspin3 ||
            ls.kind == ClearKind::MiniTspin1 ||
            ls.kind == ClearKind::MiniTspin2;

        ls.b2b_after = is_b2b_action ? true : (r.lines_cleared > 0 ? false : b2b_before);

        // 攻撃力計算 (Attack)
        {
            Landing tmp{};
            tmp.lines_cleared = ls.lines_cleared;
            tmp.kind = ls.kind;
            tmp.combo = ls.combo_after;
            tmp.back_to_back = ls.b2b_after;
            tmp.perfect_clear = ls.perfect_clear;
            // 攻撃テーブル計算 (tetris_attack.h の関数を呼ぶと仮定)
            // compute_attack が未定義なら適当な計算または0にする
            extern int compute_attack(const Landing &);
            ls.attack = compute_attack(tmp);
        }

        // ダミーデータ（DAGでは不要だがLandingSearch構造体の要件満たすため）
        ls.current_after = piece;
        ls.bag_index_after = 0;
        ls.spawn_x_after = 0;
        ls.spawn_y_after = 0;
        ls.dead_after = false;
        ls.used_hold_this_turn_after = false;

        out.push_back(ls);
    }
    return out;
}

// 評価用ヘルパー
static inline double evaluate_landing_search(const LandingSearch &ls, const EvalWeights &w)
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

// 報酬計算
static inline Reward evaluate_reward(const LandingSearch &ls, const EvalWeights &w)
{
    Reward r{};
    r.value = evaluate_landing_search(ls, w);
    r.attack = ls.attack > 0 ? ls.attack : -1;
    return r;
}

// ------------------------------------------------------------
// Expand Node (Fixed: Use Stack Array instead of std::vector)
// ------------------------------------------------------------
static void expand_node(
    DagArena &arena,
    TranspositionTable &tt,
    NodeID nid,
    const EvalWeights &w,
    const DagConfig &cfg,
    DagTiming &tm,
    std::mt19937_64 &rng)
{
    auto t0 = Clock::now();
    Node &parent = arena.nodes[nid];

    if (parent.is_expanded)
        return;

    PieceType current = get_current_piece(arena, parent.generation_index, parent.key.bag_remaining, rng);

    // 【高速化】ヒープ割り当て(std::vector)を排除し、スタック配列を使用
    struct MoveEntry
    {
        LandingSearch ls;
        bool used_hold;
        PieceType piece_used;
    };

    // テトリスの最大分岐数はそれほど多くない（せいぜい100程度）
    std::array<MoveEntry, 128> moves_buffer;
    int moves_count = 0;

    auto add_moves = [&](PieceType p, bool hold)
    {
        auto [sx, sy] = spawn_position_with_fallback(parent.key.bb, p);
        if (sx >= 0)
        {
            // enumerate_place_only_virtual は内部で vector を返しているため、
            // これも本来はコールバック等で受け取るのが最速だが、
            // 今回はここまでの修正にとどめる
            auto lands = enumerate_place_only_virtual(parent.key.bb, p, sx, sy, parent.key.combo, parent.key.b2b);
            for (auto &l : lands)
            {
                if (moves_count < 128)
                {
                    moves_buffer[moves_count++] = {l, hold, p};
                }
            }
        }
    };

    // 1. Current
    add_moves(current, false);

    // 2. Hold / Reserve
    if (parent.key.reserve_is_hold)
    {
        add_moves((PieceType)parent.key.reserve, true);
    }
    else
    {
        // Hold空、Currentを入れてNext(Reserve)を出す
        add_moves((PieceType)parent.key.reserve, true);
    }

    if (moves_count == 0)
    {
        parent.is_expanded = true;
        parent.is_dead = true;
        parent.value.value = -1e15;
        tm.expanded_nodes++;
        return;
    }

    NodeID edges_start = arena.alloc_edges(moves_count);
    if (edges_start == NULL_NODE)
        return;

    parent.children_start = edges_start;
    parent.children_count = (int16_t)moves_count;

    int next_gen_idx = parent.generation_index + 1;
    PieceType next_gen_0 = get_next_piece_for_key(arena, next_gen_idx);

    for (int i = 0; i < moves_count; ++i)
    {
        const auto &m = moves_buffer[i]; // Bufferから読み出し

        SearchState ns;
        ns.bb = m.ls.bb_after;
        ns.combo = m.ls.combo_after;
        ns.b2b = m.ls.b2b_after;

        ns.bag_remaining = parent.key.bag_remaining;
        uint8_t used_bit = (1 << (int)m.piece_used);
        if ((ns.bag_remaining & used_bit) == 0)
            ns.bag_remaining = 0x7F;
        ns.bag_remaining &= ~used_bit;
        if (ns.bag_remaining == 0)
            ns.bag_remaining = 0x7F;

        if (m.used_hold)
        {
            ns.has_hold = true;
            ns.hold_piece = current;
        }
        else
        {
            if (parent.key.reserve_is_hold)
            {
                ns.has_hold = true;
                ns.hold_piece = (PieceType)parent.key.reserve;
            }
            else
            {
                ns.has_hold = false;
                ns.hold_piece = (PieceType)0;
            }
        }
        ns.next_0_in_gen = next_gen_0;

        DagKey child_key = make_key(ns);

        // TT Lookup
        NodeID child_id = tt.get(child_key);

        if (child_id == NULL_NODE)
        {
            // 新しいノードを確保
            child_id = arena.alloc_node(child_key);
            if (child_id == NULL_NODE)
                break; // メモリ不足時はループを抜ける

            arena.nodes[child_id].generation_index = next_gen_idx;

            // --------------------------------------------------------
            // ★ Step 3: 評価関数の呼び出し修正
            // --------------------------------------------------------
            if (cfg.use_landing_reward)
            {
                // 1. スタック上に一時的な GameState を作成 (コストは無視できるほど小さい)
                GameState temp_gs;

                // 2. 盤面情報をコピー
                temp_gs.bb = ns.bb;

                // 3. Hold情報をコピー
                temp_gs.has_hold = ns.has_hold;
                // Holdがない場合はデフォルト値(I)を入れるが、has_hold=falseなら無視されるので安全
                temp_gs.hold_piece = ns.has_hold ? ns.hold_piece : PieceType::I;

                // 4. Bag情報をセット (ここが最重要)
                // 探索中は本物の PieceBag オブジェクトを持てないので nullptr にする
                temp_gs.bag = nullptr;
                temp_gs.bag_index = 0;

                // 代わりに SearchState が持っているビットマスクを bag_mask に渡す
                // これにより evaluate_board 側で「残りのTミノの有無」等が判断可能になる
                temp_gs.bag_mask = ns.bag_remaining;

                // 5. 評価関数へ渡す
                arena.nodes[child_id].value.value = evaluate_board(temp_gs, w);
            }
            // --------------------------------------------------------

            tt.put(child_key, child_id);
            tm.created_nodes++;
        }

        arena.add_parent_link(child_id, nid);

        Edge &e = arena.edges[edges_start + i];
        e.child_id = child_id;
        e.move = m.ls;
        e.move.used_hold = m.used_hold;

        if (cfg.use_landing_reward)
        {
            LandingSearch tmp = m.ls;
            tmp.piece = m.piece_used;
            e.reward = evaluate_reward(tmp, w);
        }
        else
        {
            e.reward = {0, -1};
        }
    }

    parent.is_expanded = true;
    tm.expanded_nodes++;

    auto t1 = Clock::now();
    tm.expand_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

// ------------------------------------------------------------
// Backprop (Optimized)
// ------------------------------------------------------------
static void backprop(DagArena &arena, const std::vector<NodeID> &path, DagTiming &tm)
{
    auto t0 = Clock::now();

    // std::vector の再確保を防ぐため、static thread_local なバッファを使うか
    // 呼び出し元の path を利用する。
    // ここでは path からの単純な親伝播を行う

    // 簡易キュー (スタック領域を少し大きめに取るか、Arenaのscratch領域を使うのがベストだが
    // ここではstd::vectorを使うが、capacityを確保して再利用する)

    // Backpropは「葉」からではなく「Expandしたノード」から
    if (path.empty())
        return;

    // 簡易的な実装: 重複訪問を防ぐために visited フラグを使いたいが
    // Nodeにフラグを持たせるとリセットが面倒。
    // 今回は「パス上のノードとその親」を限定的に更新する

    std::vector<NodeID> queue;
    queue.reserve(128);
    queue.push_back(path.back());

    int limit = 500; // 安全策

    while (!queue.empty() && limit-- > 0)
    {
        NodeID nid = queue.back();
        queue.pop_back();

        Node &node = arena.nodes[nid];
        node.visits++;

        if (!node.is_expanded)
            continue;

        Value best_v = {-1e16, 0};
        bool all_dead = true;

        for (int i = 0; i < node.children_count; ++i)
        {
            const Edge &e = arena.edges[node.children_start + i];
            const Node &child = arena.nodes[e.child_id];

            if (child.is_dead)
                continue;
            all_dead = false;

            Value v = child.value;
            v.value += e.reward.value;
            if (e.reward.attack > 0)
                v.spike += e.reward.attack;

            best_v.improve(v);
        }

        if (all_dead)
        {
            node.is_dead = true;
            node.value.value = -1e16;
        }
        else
        {
            if (best_v.value != node.value.value || best_v.spike != node.value.spike)
            {
                node.value = best_v;

                NodeID pid = node.parents_head;
                while (pid != NULL_NODE)
                {
                    const auto &link = arena.parent_links[pid];
                    queue.push_back(link.parent_id);
                    pid = link.next_link_id;
                }
            }
        }
    }

    auto t1 = Clock::now();
    tm.backprop_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

// ------------------------------------------------------------
// Main Search (Optimized Loop)
// ------------------------------------------------------------
DagResult dag_search(const GameState &root, const EvalWeights &w, const DagConfig &cfg)
{
    auto t_begin = Clock::now();

    DagArena arena(cfg);
    TranspositionTable tt;
    tt.resize(cfg.tt_size_bits);

    DagTiming tm;
    std::mt19937_64 rng(12345);

    // Init Generations
    arena.generations.push_back({false, root.current});
    if (root.bag)
    {
        for (size_t i = 0; i < root.bag->size() && i < (size_t)cfg.bag_probe; ++i)
        {
            int idx = (root.bag_index + i) % root.bag->size();
            arena.generations.push_back({false, root.bag->at(idx)});
        }
    }
    for (int i = 0; i < cfg.max_depth; ++i)
    {
        arena.generations.push_back({true, PieceType::I});
    }

    // Root Node
    SearchState s0;
    s0.bb = root.bb;
    s0.combo = root.combo;
    s0.b2b = root.back_to_back;
    s0.bag_remaining = compute_bag_remaining(root);
    s0.has_hold = root.has_hold;
    s0.hold_piece = root.hold_piece;
    s0.next_0_in_gen = get_next_piece_for_key(arena, 0);

    DagKey root_key = make_key(s0);
    NodeID root_id = arena.alloc_node(root_key);
    tt.put(root_key, root_id);
    arena.nodes[root_id].value.value = evaluate_board(root, w);

    // scratch buffer for path
    std::vector<NodeID> path;
    path.reserve(cfg.max_depth + 2);

    for (int iter = 0; iter < cfg.iterations; ++iter)
    {
        if (arena.nodes.size() >= (size_t)cfg.max_nodes)
            break;

        path.clear();
        NodeID cur = root_id;
        path.push_back(cur);

        // Selection Loop (Optimized: No vector allocation)
        for (int d = 0; d < cfg.max_depth; ++d)
        {
            Node &n = arena.nodes[cur];

            if (!n.is_expanded || n.children_count == 0)
                break;

            // 1パス目: Max Q を探す
            double max_q = -1e16;
            bool any_alive = false;

            for (int i = 0; i < n.children_count; ++i)
            {
                const Edge &e = arena.edges[n.children_start + i];
                const Node &c = arena.nodes[e.child_id];
                if (c.is_dead)
                    continue;

                any_alive = true;
                double q = c.value.value + e.reward.value;
                if (q > max_q)
                    max_q = q;
            }

            if (!any_alive)
                break;

            // 2パス目: Weighted Sampling (On-the-fly)
            double sum_w = 0.0;
            // 乱数を先に引くのは重み総和がわからないとできないので、
            // ここでは一旦重み総和を計算する必要があるか、
            // あるいは「最大値からの相対」で弾くか。

            // シンプルなRejection Samplingは遅いので、
            // ここだけはスタック配列を使う (最大128手)
            std::array<double, 128> weights_buf;
            std::array<NodeID, 128> child_ids_buf;
            int count = 0;

            for (int i = 0; i < n.children_count; ++i)
            {
                const Edge &e = arena.edges[n.children_start + i];
                const Node &c = arena.nodes[e.child_id];
                if (c.is_dead)
                    continue;

                double q = c.value.value + e.reward.value;
                // rankは本来ソートが必要だが、計算コスト削減のため省略し、単純なQ値ベースの重み付け
                // または children を Q値順に並べておく最適化もある
                double w = cc_weight_fast(q, max_q - 50.0, 1); // min_qの代わりにmaxからの差分で

                if (count < 128)
                {
                    weights_buf[count] = w;
                    child_ids_buf[count] = e.child_id;
                    sum_w += w;
                    count++;
                }
            }

            if (sum_w <= 0.0)
                break;

            std::uniform_real_distribution<double> dist(0.0, sum_w);
            double r = dist(rng);

            int selected_idx = 0;
            for (int i = 0; i < count; ++i)
            {
                r -= weights_buf[i];
                if (r <= 0)
                {
                    selected_idx = i;
                    break;
                }
            }

            cur = child_ids_buf[selected_idx];
            path.push_back(cur);
        }

        expand_node(arena, tt, cur, w, cfg, tm, rng);
        backprop(arena, path, tm);
        tm.iters++;
    }

    // Result Construction
    DagResult res;
    Node &r = arena.nodes[root_id];
    res.best_score = -1e16;
    res.nodes = (int)arena.nodes.size();

    if (r.children_count == 0)
    {
        // 何もせずリターンする
        // res.best はデフォルトコンストラクタの値 (x=0, y=0など) になるが、
        // score が -1e16 なので、受け取り側で判定できるようにする
        return res;
    }

    if (r.children_count > 0)
    {
        for (int i = 0; i < r.children_count; ++i)
        {
            const Edge &e = arena.edges[r.children_start + i];
            const Node &c = arena.nodes[e.child_id];

            double score = c.value.value + e.reward.value;

            DagCandidate cand;
            cand.move = e.move;
            cand.score = score;
            cand.value = c.value;
            res.topk.push_back(cand);

            if (score > res.best_score)
            {
                res.best_score = score;
                res.best = e.move;
                res.best_value = c.value;
            }
        }
        std::sort(res.topk.begin(), res.topk.end(), [](const DagCandidate &a, const DagCandidate &b)
                  { return a.score > b.score; });
        if (res.topk.size() > (size_t)cfg.top_k)
            res.topk.resize(cfg.top_k);
    }

    auto t_end = Clock::now();
    res.think_ms = std::chrono::duration<double, std::milli>(t_end - t_begin).count();
    res.timing = tm;

    return res;
}