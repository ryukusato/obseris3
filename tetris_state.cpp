// ===============================
// FINAL STATE MANAGEMENT (Next / Hold safe)
// ===============================

#include "tetris_state.h"
#include "tetris_garbage.h"
#include "tetris_attack.h"
#include "tetris_gameover.h"
#include "tetris_engine.h"
#include "tetris_step.h"

// ------------------------------------------------
// GameState init
// ------------------------------------------------
void GameState::init(PieceBag *my_bag)
{
    bag = my_bag;

    // first piece is consumed immediately
    bag_index = 1;

    bb = {}; // empty board
    combo = 0;
    back_to_back = false;
    pending_garbage = 0;

    has_hold = false;
    used_hold_this_turn = false;

    current = bag->at(0);

    auto [sx, sy] = spawn_position_with_fallback(bb, current);
    spawn_x = sx;
    spawn_y = sy;
}

// ------------------------------------------------
// MatchState
// ------------------------------------------------
MatchState::MatchState(unsigned int seed1, unsigned int seed2)
{
    bags[0] = std::make_shared<PieceBag>(seed1);
    bags[1] = std::make_shared<PieceBag>(seed2);

    players[0].init(bags[0].get());
    players[1].init(bags[1].get());

    turn = 0;
}

MatchState::MatchState(const MatchState &other)
{
    bags[0] = std::make_shared<PieceBag>(*other.bags[0]);
    bags[1] = std::make_shared<PieceBag>(*other.bags[1]);

    players[0] = other.players[0];
    players[1] = other.players[1];
    turn = other.turn;

    players[0].bag = bags[0].get();
    players[1].bag = bags[1].get();
}

MatchState &MatchState::operator=(const MatchState &other)
{
    if (this == &other)
        return *this;

    bags[0] = std::make_shared<PieceBag>(*other.bags[0]);
    bags[1] = std::make_shared<PieceBag>(*other.bags[1]);

    players[0] = other.players[0];
    players[1] = other.players[1];
    turn = other.turn;

    players[0].bag = bags[0].get();
    players[1].bag = bags[1].get();
    return *this;
}

// ------------------------------------------------
// LEGAL MOVES
// ------------------------------------------------
std::vector<Landing> legal_moves(const GameState &s)
{
    if (s.spawn_x < 0)
        return {};

    std::vector<Landing> moves;
    moves.reserve(128);

    // ---- normal piece ----
    auto normal = enumerate_landings(
        s.bb, s.current,
        s.spawn_x, s.spawn_y,
        s.combo, s.back_to_back);

    for (auto &l : normal)
    {
        l.used_hold = false;
        l.piece_after_hold = l.piece;
        moves.push_back(std::move(l));
    }

    // ---- hold ----
    if (!s.used_hold_this_turn)
    {
        PieceType new_current = s.has_hold
                                    ? s.hold_piece
                                    : s.bag->at(s.bag_index);

        auto [sx, sy] = spawn_position_with_fallback(s.bb, new_current);
        if (sx >= 0)
        {
            auto hold_moves = enumerate_landings(
                s.bb, new_current,
                sx, sy,
                s.combo, s.back_to_back);

            for (auto &l : hold_moves)
            {
                l.used_hold = true;
                l.piece_after_hold = new_current;
                moves.push_back(std::move(l));
            }
        }
    }

    return moves;
}

// ------------------------------------------------
// REAL MOVE (used in match)
// ------------------------------------------------
GameState apply_move(GameState s, const Landing &l)
{
    return simulate_move(s, l);
}

// ------------------------------------------------
// ATTACK / GARBAGE
// ------------------------------------------------
void apply_attack(GameState &attacker, GameState &defender, const Landing &l)
{
    int atk = l.attack;
    if (atk <= 0)
        return;

    int cancel = std::min(atk, defender.pending_garbage);
    atk -= cancel;
    defender.pending_garbage -= cancel;

    defender.pending_garbage += atk;
}

void apply_attack_search(GameState &attacker, GameState &defender, const LandingSearch &l)
{
    int atk = l.attack;
    if (atk <= 0)
        return;

    int cancel = std::min(atk, defender.pending_garbage);
    atk -= cancel;
    defender.pending_garbage -= cancel;

    defender.pending_garbage += atk;
}

void apply_pending_garbage(GameState &s, int hole_x)
{
    if (s.pending_garbage <= 0)
        return;

    int applied = apply_garbage(s.bb, s.pending_garbage, hole_x, s.max_receive);
    s.pending_garbage -= applied;
}

// ------------------------------------------------
// TURN CONTROL
// ------------------------------------------------
void start_turn(GameState &s)
{
    static bool initialized = false;
    if (!initialized)
    {
        init_piece_tables();
        initialized = true;
    }
    if (s.dead)
        return;
    s.used_hold_this_turn = false;

    int hole = s.bag->rand_int(BOARD_WIDTH);
    apply_pending_garbage(s, hole);

    auto [sx, sy] = spawn_position_with_fallback(s.bb, s.current);
    s.spawn_x = sx;
    s.spawn_y = sy;
}

// ------------------------------------------------
// MATCH HELPERS
// ------------------------------------------------
GameState &current_player(MatchState &m) { return m.players[m.turn]; }
const GameState &current_player(const MatchState &m) { return m.players[m.turn]; }
GameState &opponent_player(MatchState &m) { return m.players[m.turn ^ 1]; }
const GameState &opponent_player(const MatchState &m) { return m.players[m.turn ^ 1]; }

void start_turn(MatchState &m) { start_turn(current_player(m)); }

void play_move(MatchState &m, const Landing &l)
{
    GameState &me = current_player(m);
    GameState &opp = opponent_player(m);

    me = apply_move(me, l);
    apply_attack(me, opp, l);

    m.turn ^= 1;
}

// ============================================================
// reconstruct Landing from LandingSearch + root GameState
// ============================================================
Landing reconstruct_landing(
    const GameState &root,
    const LandingSearch &ls)
{
    Landing l{};

    l.piece = ls.piece;
    l.final_x = ls.final_x;
    l.final_y = ls.final_y;
    l.final_rot = ls.final_rot;
    l.used_hold = ls.used_hold;

    // lock simulation
    StepResult r = step_lock_piece_at(
        root.bb,
        ls.piece,
        ls.final_rot,
        ls.final_x,
        ls.final_y);

    l.bb_after = r.bb_after;
    l.lines_cleared = r.lines_cleared;
    l.perfect_clear = r.perfect_clear;
    l.kind = ls.kind;

    // combo / b2b
    l.combo = (r.lines_cleared > 0 ? root.combo + 1 : 0);
    l.back_to_back = ls.b2b_after;

    // attack
    l.attack = ls.attack;

    return l;
}

void play_move_search(MatchState &m, const LandingSearch &ls)
{
    GameState &me = current_player(m);
    GameState &opp = opponent_player(m);

    Landing l = reconstruct_landing(me, ls);
    me = simulate_move(me, l);
    apply_attack(me, opp, l);

    m.turn ^= 1;
}

// ------------------------------------------------
// UTIL
// ------------------------------------------------
std::vector<PieceType> next_pieces(const GameState &s, int n)
{
    std::vector<PieceType> out;
    out.reserve(n);

    for (int i = 0; i < n; ++i)
        out.push_back(s.bag->at(s.bag_index + i));

    return out;
}

bool is_dead_state(const GameState &s)
{
    return is_dead(s.bb, s.current);
}
