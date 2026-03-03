#include "tetris_garbage.h"
#include <algorithm>

int apply_garbage(BitBoard &bb, int lines, int hole_x, int max_receive)
{
    int applied = std::min(lines, max_receive);
    if (applied <= 0)
        return 0;

    // hole列だけ0、それ以外1
    const BoardBits FULL_ROW = (BoardBits(1) << BOARD_WIDTH) - 1;
    BoardBits garbage = FULL_ROW;
    if (0 <= hole_x && hole_x < BOARD_WIDTH)
        garbage = BoardBits(FULL_ROW & ~(BoardBits(1) << hole_x));

    for (int i = 0; i < applied; ++i)
    {
        // 上に押し上げ：row[y] = row[y-1]
        for (int y = TOTAL_BOARD_HEIGHT - 1; y > 0; --y)
            bb.row[y] = bb.row[y - 1];

        bb.row[0] = garbage;
    }

    return applied;
}
