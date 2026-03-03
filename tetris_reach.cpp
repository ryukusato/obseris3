// tetris_reach.cpp
#include "tetris_core.h"
#include "tetris_rules.h"

#include <queue>
#include <set>

struct Node {
    int x;
    int y;
    int rot;
};


bool can_reach(const Board& board,
    PieceType piece,
    int start_x, int start_y, int start_rot,
    int target_x, int target_y, int target_rot)
{
std::queue<Node> q;
std::set<std::tuple<int,int,int>> visited;

q.push({start_x, start_y, start_rot});
visited.insert({start_x, start_y, start_rot});

while (!q.empty()) {
Node cur = q.front(); q.pop();

if (cur.x == target_x &&
cur.y == target_y &&
cur.rot == target_rot)
return true;

// 現在の形
const Coords& shape = get_shape(piece, cur.rot);

// --- 平行移動（左・右・下） ---
const int dx[3] = {-1, 1, 0};
const int dy[3] = { 0, 0,-1};

for (int i = 0; i < 3; ++i) {
int nx = cur.x + dx[i];
int ny = cur.y + dy[i];
int nr = cur.rot;

if (!visited.count({nx,ny,nr}) &&
is_valid_position(board, shape, nx, ny))
{
visited.insert({nx,ny,nr});
q.push({nx,ny,nr});
}
}

// --- 回転（時計回り / 反時計回り） ---
for (int dir = 0; dir < 2; ++dir) {
bool cw = (dir == 0);
int next_rot = (cur.rot + (cw ? 1 : 3)) % 4;

const Coords& next_shape = get_shape(piece, next_rot);
const auto& kicks = get_kicks(piece, cur.rot, next_rot);

for (auto [kx, ky] : kicks) {
int nx = cur.x + kx;
int ny = cur.y + ky;
int nr = next_rot;

if (visited.count({nx,ny,nr})) continue;
if (!is_valid_position(board, next_shape, nx, ny)) continue;

visited.insert({nx,ny,nr});
q.push({nx,ny,nr});
}
}
}
return false;
}
