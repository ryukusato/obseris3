#pragma once
#include <vector>
#include <memory>
#include <array>
#include <utility>
#include "tetris_board.h"
#include "tetris_piece.h"
#include "tetris_search.h"
#include "tetris_bag.h"

// =============================
// GameState（1P状態）
// =============================
struct GameState
{
    // --- board ---
    BitBoard bb{};

    // --- piece system ---
    PieceBag *bag = nullptr;
    int bag_index = 0;
    uint8_t bag_mask = 0;

    PieceType current{};
    PieceType hold_piece{};
    bool has_hold = false;
    bool used_hold_this_turn = false;

    // --- spawn ---
    int spawn_x = -1;
    int spawn_y = -1;
    bool dead = false;

    // --- combo / b2b ---
    int combo = 0;
    bool back_to_back = false;

    // --- garbage ---
    int pending_garbage = 0;
    int max_receive = 10;

    // --- init ---
    void init(PieceBag *my_bag);
};

// =============================
// MatchState（2P状態）
// =============================
struct MatchState
{
    std::shared_ptr<PieceBag> bags[2];
    GameState players[2];
    int turn = 0;

    MatchState(unsigned int seed1, unsigned int seed2);
    MatchState(const MatchState &other);
    MatchState &operator=(const MatchState &other);
};

// =============================
// 基本API
// =============================

std::vector<Landing> legal_moves(const GameState &s);


GameState apply_move(GameState s, const Landing &l);

bool is_dead_state(const GameState &s);

// --- garbage / attack ---
void apply_attack(GameState &attacker, GameState &defender, const Landing &l);
void apply_attack_search(GameState &attacker, GameState &defender, const LandingSearch &l);
void apply_pending_garbage(GameState &s, int hole_x);

// --- turn ---
void start_turn(GameState &s);
void end_turn(GameState &s);

GameState &current_player(MatchState &m);
const GameState &current_player(const MatchState &m);

GameState &opponent_player(MatchState &m);
const GameState &opponent_player(const MatchState &m);

void start_turn(MatchState &m);
void play_move(MatchState &m, const Landing &l);
void play_move_search(MatchState &m, const LandingSearch &ls);

// --- next pieces ---
std::vector<PieceType> next_pieces(const GameState &s, int n);
