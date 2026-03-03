#include "tetris_state.h"
#include "tetris_attack.h"
#include "tetris_gameover.h"
#include "tetris_garbage.h"
#include "tetris_duel.h"

//
// ===============================
// 探索用：副作用なし1手進行
// ===============================
//
GameState simulate_move(GameState s, const Landing &l)
{

    // --- board ---
    s.bb = l.bb_after;

    // --- combo ---
    if (l.lines_cleared > 0)
        s.combo++;
    else
        s.combo = 0;

    // --- B2B ---
    const bool is_b2b =
        l.kind == ClearKind::Tspin1 ||
        l.kind == ClearKind::Tspin2 ||
        l.kind == ClearKind::Tspin3 ||
        l.kind == ClearKind::MiniTspin1 ||
        l.kind == ClearKind::MiniTspin2 ||
        l.kind == ClearKind::Clear4;

    if (is_b2b)
        s.back_to_back = true;
    else if (l.lines_cleared > 0)
        s.back_to_back = false;

    // --- hold ---
    if (l.used_hold)
    {
        if (!s.has_hold)
        {
            s.hold_piece = s.current;
            s.has_hold = true;
            s.current = s.bag->at(s.bag_index++);
        }
        else
        {
            std::swap(s.current, s.hold_piece);
        }
    }

    // --- next piece ---
    s.current = s.bag->at(s.bag_index++);

    // --- spawn ---
    auto [sx, sy] = spawn_position_with_fallback(s.bb, s.current);
    s.spawn_x = sx;
    s.spawn_y = sy;
    if (sx < 0)
    {
        s.dead = true; // ★ 必須
        return s;      // ★ これ以上進めない
    }

    // --- reset hold flag ---
    s.used_hold_this_turn = false;

    return s;
}

//
// ===============================
// 実ゲーム用：1ターン進行
// ===============================
//
void engine_step(MatchState &m, const Landing &l)
{
    GameState &me = current_player(m);
    GameState &opp = opponent_player(m);

    // --- 自分の状態更新 ---
    me = simulate_move(me, l);

    // --- 攻撃 ---
    apply_attack(me, opp, l);

    // --- 手番交代 ---
    m.turn ^= 1;

    // --- 次ターン開始処理 ---
    GameState &next = current_player(m);

    // pending garbage適用
    int hole = next.bag->rand_int(BOARD_WIDTH);
    apply_pending_garbage(next, hole);

    // spawn更新（currentはsimulate_moveで確定済）
    auto [sx, sy] = spawn_position_with_fallback(next.bb, next.current);
    next.spawn_x = sx;
    next.spawn_y = sy;

    next.used_hold_this_turn = false;
}

//
// ===============================
// 試合終了判定
// ===============================
//
bool engine_is_done(const MatchState &m)
{
    return is_dead_state(m.players[0]) ||
           is_dead_state(m.players[1]);
}

//
// ===============================
// 勝者判定
// ===============================
//
Winner engine_winner(const MatchState &m)
{
    const bool d0 = is_dead_state(m.players[0]);
    const bool d1 = is_dead_state(m.players[1]);

    if (d0 && d1)
        return Winner::Draw;
    if (d0)
        return Winner::Player2;
    if (d1)
        return Winner::Player1;

    return Winner::None;
}
