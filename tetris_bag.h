#pragma once
#include "tetris_piece.h"
#include <random>
#include <deque>
#include <array>

class PieceBag
{
public:
    explicit PieceBag(unsigned int seed = std::random_device{}());

    // idx 番目のミノを取得（非破壊）
    PieceType at(int idx);
    int rand_int(int mod);
    size_t size() const;

private:
    std::mt19937 rng;
    std::deque<PieceType> queue;

    void refill();
    void ensure(int idx);
};
