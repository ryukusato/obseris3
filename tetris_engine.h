#pragma once
#include "tetris_state.h"
#include "tetris_duel.h"

GameState simulate_move(GameState s, const Landing &l);

void engine_step(MatchState &m, const Landing &l);

bool engine_is_done(const MatchState &m);

Winner engine_winner(const MatchState &m);
