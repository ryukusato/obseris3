#include "tetris_duel.h"
#include "tetris_gameover.h"

Winner judge_winner(const BitBoard& b1, PieceType p1,
                    const BitBoard& b2, PieceType p2)
{
    bool dead1 = is_dead(b1, p1);
    bool dead2 = is_dead(b2, p2);

    if (dead1 && dead2) return Winner::Draw;
    if (dead1) return Winner::Player2;
    if (dead2) return Winner::Player1;
    return Winner::None;
}
