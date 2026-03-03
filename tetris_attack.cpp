#include "tetris_attack.h"
#include <algorithm>


// CC と同様の簡易 combo テーブル
static const int COMBO_TABLE[12] = {
    0,0,1,1,2,2,3,3,4,4,4,5
};

static const int KIND_ATTACK[] = {
    0, // None
    0, // Clear1
    1, // Clear2
    2, // Clear3
    4, // Clear4
    2, // Tspin1
    4, // Tspin2
    6, // Tspin3
    0, // MiniTspin1
    1  // MiniTspin2
};

int compute_attack(const Landing& l)
{
    int atk = KIND_ATTACK[(int)l.kind];

    // B2B ボーナス
    bool is_b2b_action =
           l.kind == ClearKind::Tspin1
        || l.kind == ClearKind::Tspin2
        || l.kind == ClearKind::Tspin3
        || l.kind == ClearKind::MiniTspin1
        || l.kind == ClearKind::MiniTspin2
        || l.kind == ClearKind::Clear4;

    if(is_b2b_action && l.back_to_back)
        atk += 1;

    // combo ボーナス
    if(l.lines_cleared > 0){
        int c = std::min(l.combo, 11);
        atk += COMBO_TABLE[c];
    }

    // Perfect Clear 
    if(l.perfect_clear)
        atk = 10;

    if (atk > 0)
    {

    }

    return atk;
}

