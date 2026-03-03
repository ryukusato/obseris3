// tetris_piece.h
#pragma once
#include <array>
#include <vector>
#include <utility>
#include <cstdint>

enum class PieceType
{
    I,
    O,
    T,
    S,
    Z,
    J,
    L
};
constexpr int PieceTypeCount = 7;

using Coords = std::vector<std::pair<int, int>>;
using BoardBits = uint16_t;

struct PieceBB
{
    int minx, miny;
    int w, h;
    uint16_t rowmask[4];
};
extern const std::array<std::array<Coords, 4>, PieceTypeCount> SHAPES;
extern PieceBB PIECE_BB[PieceTypeCount][4];
const Coords &get_shape(PieceType p, int rot);
constexpr int PIECE_COUNT = 7;

constexpr std::array<PieceType, PIECE_COUNT> ALL_PIECES = {
    PieceType::I, PieceType::O, PieceType::T,
    PieceType::S, PieceType::Z, PieceType::J, PieceType::L};

void init_piece_tables();