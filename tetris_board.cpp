// tetris_board.cpp
#include "tetris_board.h"
#include "tetris_piece.h"
#include <vector>

// ===== Board → BitBoard =====
BitBoard make_bitboard(const Board &b)
{
    BitBoard bb{};
    for (int y = 0; y < TOTAL_BOARD_HEIGHT; y++)
    {
        uint16_t m = 0;
        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            if (b[y][x])
                m |= (uint16_t(1) << x);
        }
        bb.row[y] = m;
    }
    return bb;
}

// ===== 衝突判定（探索用・最速） =====
bool is_valid_position(
    const BitBoard &bb,
    PieceType piece,
    int rot,
    int x,
    int y)
{

    const auto &p = PIECE_BB[(int)piece][rot & 3];

    int bx = x + p.minx;
    int by = y + p.miny;

    if ((unsigned)bx > BOARD_WIDTH - p.w)
        return false;
    if ((unsigned)by > TOTAL_BOARD_HEIGHT - p.h)
        return false;

    for (int dy = 0; dy < p.h; dy++)
    {
        uint16_t m = p.rowmask[dy] << bx;
        if (bb.row[by + dy] & m)
            return false;
    }
    return true;
}

// ===== ロック処理（評価用） =====
Board place_piece(
    const Board &board,
    PieceType piece,
    int rot,
    int x,
    int y)
{
    Board out = board;
    const Coords &sh = get_shape(piece, rot);

    for (auto [dx, dy] : sh)
    {
        int px = x + dx;
        int py = y + dy;
        out[py][px] = 1;
    }
    return out;
}

// ===== ライン消去 =====
std::pair<Board, int> clear_lines(const Board &board)
{
    Board out(TOTAL_BOARD_HEIGHT, std::vector<int>(BOARD_WIDTH, 0));
    int write = 0;
    int cleared = 0;

    for (int y = 0; y < TOTAL_BOARD_HEIGHT; y++)
    {
        bool full = true;
        for (int x = 0; x < BOARD_WIDTH; x++)
        {
            if (!board[y][x])
            {
                full = false;
                break;
            }
        }
        if (full)
        {
            cleared++;
        }
        else
        {
            out[write++] = board[y];
        }
    }
    return {out, cleared};
}
