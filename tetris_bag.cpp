#include "tetris_bag.h"
#include <algorithm>

PieceBag::PieceBag(unsigned int seed) : rng(seed)
{
    refill();
    refill(); // 2bag 先まで確保
}

void PieceBag::refill()
{
    std::array<PieceType, PIECE_COUNT> bag = ALL_PIECES;
    std::shuffle(bag.begin(), bag.end(), rng);
    for (auto p : bag)
        queue.push_back(p);
}

void PieceBag::ensure(int idx)
{
    while ((int)queue.size() <= idx)
        refill();
}

PieceType PieceBag::at(int idx)
{
    ensure(idx);
    return queue[idx];
}
int PieceBag::rand_int(int mod)
{
    // mod > 0 前提
    return (int)(rng() % (unsigned)mod);
}
size_t PieceBag::size() const
{
    return queue.size();
}