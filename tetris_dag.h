// tetris_dag.h 
#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <limits>

#include "tetris_state.h"
#include "tetris_eval.h"
#include "tetris_search.h" // LandingSearch / enumerate_landings_search

struct DagTiming
{
    // totals (ns)
    long long total_ns = 0;
    long long select_ns = 0;
    long long leaf_ns = 0;
    long long expand_ns = 0;
    long long backprop_ns = 0;

    // expand breakdown
    long long enum_ns = 0;
    long long create_ns = 0; // get_or_create + key/hash
    long long reward_ns = 0; // evaluate_landing_search
    long long push_parent_ns = 0;

    // counts
    long long iters = 0;
    long long expanded_nodes = 0;
    long long moves_enum = 0;
    long long created_nodes = 0;
    long long parent_links = 0;
    long long leaf_evals = 0;

    // max (ns)
    long long max_iter_ns = 0;
    long long max_expand_ns = 0;
    long long max_backprop_ns = 0;
    long long max_enum_ns = 0;
};
struct DagConfig
{
    int iterations = 3000;  // how many expand/evaluate cycles
    int max_nodes = 200000; // hard cap
    int max_depth = 10;      // stop selection at this depth
    double ucb_c = 40.0;    // exploration constant (tune)
    bool use_landing_reward = true;
    int top_k = 5;
    int bag_probe = 7;             // 7 or 14
    bool deterministic_root = true; // root だけ確定選択
    int tt_size_bits = 20;
};

struct Reward
{
    double value = 0.0;   // acc_eval
    double attack = -1.0; // spike 用（-1 = 無効）
};

struct Value
{
    double value = -1e16; // transient_eval
    double spike = 0.0; // 累積攻撃

    void improve(const Value &other)
    {
        if (other.value > value)
        {
            value = other.value;
            spike = other.spike;
        }
        else if (other.value == value)
        {
            if (other.spike > spike)
            {
                spike = other.spike;
            }
        }
    }
};

struct DagCandidate
{
    LandingSearch move; // 初手
    double score;       // reward + child.value
    Value value;
};
using NodeID = int32_t;
static constexpr NodeID NULL_NODE = -1;

// ------------------------------------------------------------
// 1. DagKey: 状態の一意性を定義
// Cold Clear仕様: BitBoard + (Bag, Combo, B2B) + ReservePiece
// ------------------------------------------------------------
struct DagKey
{
    BitBoard bb;           // 盤面
    uint64_t hash;         // ★ここが必要: 高速比較用のハッシュ値
    uint8_t bag_remaining; // 7bit mask
    uint8_t combo;         // 現在のREN数

    // Reserve Logic (Cold Clearのキモ)
    uint8_t reserve : 3;      // 次に使えるピース (0-6)
    bool reserve_is_hold : 1; // それはHold由来か？
    bool b2b : 1;             // B2B継続中か

    // 等価比較演算子 (Transposition Tableで使用)
    bool operator==(const DagKey &other) const
    {
        // ハッシュが一致している前提で、他のフィールドを比較
        // (高速化のため、ハッシュ不一致は呼び出し元で弾く設計)
        return bag_remaining == other.bag_remaining &&
               reserve == other.reserve &&
               reserve_is_hold == other.reserve_is_hold &&
               b2b == other.b2b &&
               combo == other.combo &&
               // BitBoardの比較 (memcmp または operator==)
               std::memcmp(&bb, &other.bb, sizeof(BitBoard)) == 0;
    }
};

// ------------------------------------------------------------
// 2. Edge: 親から子への遷移
// ------------------------------------------------------------
struct Edge
{
    NodeID child_id;
    Reward reward;
    // LandingSearch は少し大きいが、パス復元に必要。
    // メモリを極限まで削るなら move の index だけ持つ手もあるが今回は保持。
    LandingSearch move;
};

// ------------------------------------------------------------
// 3. ParentLink: DAGの親参照 (Linked List in Arena)
// Node -> ParentLink -> ParentLink -> ...
// ------------------------------------------------------------
struct ParentLink
{
    NodeID parent_id;
    NodeID next_link_id; // 次の親へのリンク
};

// ------------------------------------------------------------
// 4. Node: 探索ノード (ポインタを持たない)
// ------------------------------------------------------------
struct Node
{
    DagKey key;  // ★ここが必要: 状態キーそのもの
    Value value; // 評価値

    // グラフ構造 (Arenaのインデックス)
    NodeID children_start = NULL_NODE; // 子エッジ配列の開始位置
    int16_t children_count = 0;        // 子の数

    // ★ここが必要: 親への参照 (可変長vectorを使わず、リンクリストで管理)
    NodeID parents_head = NULL_NODE;

    int generation_index = 0; // 深さ
    int visits = 0;

    bool is_expanded : 1;
    bool is_dead : 1;

    Node() : is_expanded(false), is_dead(false) {}
};

// ------------------------------------------------------------
// 5. DagArena: 全データを保持するコンテナ
// ------------------------------------------------------------
struct DagArena
{
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<ParentLink> parent_links;

    // 世代情報: 各深さで「本来のネクスト」が何かを管理
    struct GenerationInfo
    {
        bool is_speculated;
        PieceType known_piece;
    };
    std::vector<GenerationInfo> generations;

    DagArena(const DagConfig &cfg)
    {
        nodes.reserve(cfg.max_nodes);
        edges.reserve(cfg.max_nodes * 4); // 平均分岐数4と仮定
        parent_links.reserve(cfg.max_nodes * 2);
    }

    // ノード確保
    NodeID alloc_node(const DagKey &key)
    {
        if (nodes.size() >= nodes.capacity())
            return NULL_NODE;
        nodes.emplace_back();
        NodeID id = (NodeID)(nodes.size() - 1);
        nodes[id].key = key;
        return id;
    }

    // エッジ確保 (まとめて確保することで連続性を保証)
    NodeID alloc_edges(int count)
    {
        if (edges.size() + count > edges.capacity())
            return NULL_NODE;
        NodeID start = (NodeID)edges.size();
        edges.resize(edges.size() + count);
        return start;
    }

    // 親リンク追加
    void add_parent_link(NodeID child, NodeID parent)
    {
        if (parent_links.size() >= parent_links.capacity())
            return;

        // 重複チェック（厳密にやるなら辿るが、速度優先なら省くか、直近だけ見る）
        // ここでは単純に追加
        parent_links.push_back({parent, nodes[child].parents_head});
        nodes[child].parents_head = (NodeID)(parent_links.size() - 1);
    }
};

// ------------------------------------------------------------
// 6. Transposition Table: 高速検索用ハッシュテーブル
// ------------------------------------------------------------
struct TranspositionTable
{
    struct Entry
    {
        uint64_t hash;
        NodeID node_id;
    };

    std::vector<Entry> table;
    size_t size_mask;

    void resize(int bits)
    {
        size_t sz = 1ULL << bits;
        table.assign(sz, {0, NULL_NODE});
        size_mask = sz - 1;
    }

    NodeID get(const DagKey &key) const
    {
        size_t idx = key.hash & size_mask;
        const Entry &e = table[idx];
        // Hashが一致し、NodeIDが有効なら候補。
        // ※本来はここで DagArena を参照して key == nodes[e.node_id].key を確認すべきだが
        //  高速化のため Hash (64bit) の衝突は無視する実装も多い。
        //  ここでは呼び出し元で確認することを想定。
        if (e.node_id != NULL_NODE && e.hash == key.hash)
        {
            return e.node_id;
        }
        return NULL_NODE;
    }

    void put(const DagKey &key, NodeID id)
    {
        size_t idx = key.hash & size_mask;
        // 単純な上書き (Always Replace)
        table[idx] = {key.hash, id};
    }

    void clear()
    {
        // 全クリアは重いので、探索ID(search_id)を持たせる手法もあるが、
        // 今回はシンプルに vector clear ではなく memset 0 近い処理が必要
        // または dag_search のたびに再確保せず、使い回す設計にする
        std::fill(table.begin(), table.end(), Entry{0, NULL_NODE});
    }
};

struct DagResult
{
    LandingSearch best; // best move
    double best_score;
    Value best_value;

    std::vector<DagCandidate> topk; // root topK
    int nodes;
    int expanded;
    double think_ms = 0.0;

    DagTiming timing;
};

DagResult dag_search(const GameState &root, const EvalWeights &w, const DagConfig &cfg);
