import numpy as np
import random
from dataclasses import dataclass
from typing import Optional, List, Tuple
import tetris_cpp as tc


@dataclass
class DagStepDebug:
    best_score: float = 0.0
    best_move: Optional[tc.LandingSearch] = None
    topk: List[Tuple[float, tc.LandingSearch]] = None

    board_before_clear: Optional[list] = None
    lines_cleared: int = 0
    think_ms: float = 0.0

    # DAG-specific
    nodes: int = 0
    expanded: int = 0

class DuelGameAdapterDag:
    """
    - Python が試合進行
    - 1ターンごとに C++ dag_search を呼んで LandingSearch を選ぶ
    """

    def __init__(self, seed: int = 0, top_k: int = 10):
        random.seed(seed)

        self.match = tc.MatchState(seed, seed + 1)
        self.top_k = int(top_k)

        self.debug1 = DagStepDebug(topk=[])
        self.debug2 = DagStepDebug(topk=[])

        self.done = False
        self.winner = tc.Winner.Undecided

        # DAG 設定（あなたのC++ DagConfigに合わせて）
        self.dag_cfg = tc.DagConfig()
        self.dag_cfg.iterations = 2000
        self.dag_cfg.max_depth = 30
        self.dag_cfg.max_nodes = 200000
        self.dag_cfg.ucb_c = 40.0
        self.dag_cfg.top_k = int(top_k)
        self.dag_cfg.use_landing_reward = True

        self.eval_weights = tc.EvalWeights()

        # 表示上の mode
        self.mode1 = "dag"
        self.mode2 = "dag"

        self._step_counter = 0

    # -------------------------
    # debug print
    # -------------------------
    def _debug_topk_scores(self, topk, k=10):
        if not topk:
            print("[DBG] topk empty")
            return
        print("\n[DBG] topk head:")
        for i, (sc, mv) in enumerate(topk[:k]):
            print(
                f"  {i:02d}: sc={float(sc):+.6g} "
                f"piece={int(mv.piece)} x={int(mv.final_x)} r={int(mv.final_rot)} hold={bool(mv.used_hold)} "
                f"lc={int(mv.lines_cleared)} atk={int(mv.attack)}"
            )

    def make_board_after_placement_search(self, state: tc.GameState, mv: tc.LandingSearch):
        """
        Search結果から「置いた直後(消去前)」を作りたいなら、
        C++側に get_shape_cells(piece, rot) がある前提で合成。
        ※LandingSearch は final_y を持ってるので同様に作れる
        """
        board = [row[:] for row in state.board]
        cells = tc.get_shape_cells(mv.piece, int(mv.final_rot))
        for dx, dy in cells:
            x = int(mv.final_x) + int(dx)
            y = int(mv.final_y) + int(dy)
            if 0 <= x < 10 and 0 <= y < len(board):
                board[y][x] = 1
        return board

    # -------------------------
    # 1手進める
    # -------------------------
    def step(self):
        if self.done:
            return

        tc.start_turn(self.match)

        root = self.match.player(self.match.turn)
        if tc.is_dead_state(root):
            self.winner = tc.engine_winner(self.match)
            self.done = True
            return

        # ---- DAG think ----
        t0 = tc.now_ms() if hasattr(tc, "now_ms") else None
        res = tc.dag_search(root, self.eval_weights, self.dag_cfg)
        t1 = tc.now_ms() if hasattr(tc, "now_ms") else None

        if res.best_score < -1e10:
            print(f"[DAG] Player {self.match.turn + 1} Resigned (Score: {res.best_score})")
            
            # 敗北処理
            self.done = True
            # 相手を勝者にする
            if self.match.turn == 0:
                self.winner = tc.Winner.Player2
            else:
                self.winner = tc.Winner.Player1
            return

        best = res.best
        # Python側の topk は (score, move) に揃える（viewer互換のため）
        topk = [(float(c.score), c.move) for c in list(res.topk)]
        topk.sort(key=lambda x: x[0], reverse=True)

        think_ms = float(res.think_ms) if hasattr(res, "think_ms") else (float(t1 - t0) if t0 is not None else 0.0)

        #self._debug_topk_scores(topk, k=20)

        turn = int(self.match.turn)
        me = self.match.player(turn)

        board_before = self.make_board_after_placement_search(me, best)

        dbg = DagStepDebug(
            best_score=float(res.best_score),
            best_move=best,
            topk=topk[: self.top_k],
            board_before_clear=board_before,
            lines_cleared=int(best.lines_cleared),
            think_ms=think_ms,
            nodes=int(res.nodes),
            expanded=int(res.expanded),
        )

        if turn == 0:
            self.debug1 = dbg
            self.mode1 = "dag"
        else:
            self.debug2 = dbg
            self.mode2 = "dag"

        # ---- apply move ----
        # ★ここが肝：LandingSearchを適用できるAPIが必要
        tc.play_move_search(self.match, best)

        self.winner = tc.engine_winner(self.match)
        if self.winner != tc.Winner.Undecided:
            self.done = True

        self._step_counter += 1

    # -------------------------
    # viewer getters（既存と互換）
    # -------------------------
    def is_done(self) -> bool:
        return bool(self.done)

    def get_winner(self):
        return self.winner

    def get_board1(self):
        return np.array(self.match.player(0).board, dtype=np.int32)

    def get_board2(self):
        return np.array(self.match.player(1).board, dtype=np.int32)

    def get_info1(self):
        s = self.match.player(0)
        info = {
            "mode": self.mode1,
            "eval": float(self.debug1.best_score),
            "combo": int(s.combo),
            "b2b": bool(s.back_to_back),
            "pending": int(getattr(s, "pending_garbage", 0)),
            "think_ms": float(self.debug1.think_ms),
            "turn": int(self.match.turn),

            "dag": {
                # ---- config（固定設定）----
                "config": {
                    "iters": int(self.dag_cfg.iterations),
                    "depth": int(self.dag_cfg.max_depth),
                    "nodes_cap": int(self.dag_cfg.max_nodes),
                    "ucb_c": float(self.dag_cfg.ucb_c),
                },

                # ---- result（今回の探索結果）----
                "result": {
                    "nodes": int(self.debug1.nodes),
                    "expanded": int(self.debug1.expanded),
                },
            },
        }

        # ---- timing（あれば追加）----
        if getattr(self.debug1, "timing", None) is not None:
            t = self.debug1.timing
            info["dag"]["timing"] = {
                "total_ms": float(self.debug1.think_ms),
                "enum_ms": t.enum_ns / 1e6,
                "leaf_ms": t.leaf_ns / 1e6,
                "expand_ms": t.expand_ns / 1e6,
                "backprop_ms": t.backprop_ns / 1e6,
                "max_enum_ms": t.max_enum_ns / 1e6,
                "max_leaf_ms": t.max_leaf_ns / 1e6,
            }

        return info

    def get_info2(self):
        s = self.match.player(1)
        info = {
            "mode": self.mode2,
            "eval": float(self.debug2.best_score),
            "combo": int(s.combo),
            "b2b": bool(s.back_to_back),
            "pending": int(getattr(s, "pending_garbage", 0)),
            "think_ms": float(self.debug2.think_ms),
            "turn": int(self.match.turn),

            "dag": {
                # ---- config（固定設定）----
                "config": {
                    "iters": int(self.dag_cfg.iterations),
                    "depth": int(self.dag_cfg.max_depth),
                    "nodes_cap": int(self.dag_cfg.max_nodes),
                    "ucb_c": float(self.dag_cfg.ucb_c),
                },

                # ---- result（今回の探索結果）----
                "result": {
                    "nodes": int(self.debug1.nodes),
                    "expanded": int(self.debug1.expanded),
                },
            },
        }

        # ---- timing（あれば追加）----
        if getattr(self.debug2, "timing", None) is not None:
            t = self.debug2.timing
            info["dag"]["timing"] = {
                "total_ms": float(self.debug2.think_ms),
                "enum_ms": t.enum_ns / 1e6,
                "leaf_ms": t.leaf_ns / 1e6,
                "expand_ms": t.expand_ns / 1e6,
                "backprop_ms": t.backprop_ns / 1e6,
                "max_enum_ms": t.max_enum_ns / 1e6,
            }

        return info

    def get_last_landing1(self):
        # viewerが Landing を期待するなら変換APIが必要
        return self.debug1.best_move

    def get_last_landing2(self):
        return self.debug2.best_move

    def get_topk1(self):
        return list(self.debug1.topk or [])

    def get_topk2(self):
        return list(self.debug2.topk or [])

    def get_board_before1(self):
        return self.debug1.board_before_clear

    def get_board_before2(self):
        return self.debug2.board_before_clear

    def get_next1(self, n=5):
        p = self.match.player(0)
        nxt = list(tc.next_pieces(p, int(max(0, n-1))))
        return [p.current] + nxt

    def get_next2(self, n=5):
        p = self.match.player(1)
        nxt = list(tc.next_pieces(p, int(max(0, n-1))))
        return [p.current] + nxt

    def get_hold1(self):
        p = self.match.player(0)
        return p.hold_piece if p.has_hold else None

    def get_hold2(self):
        p = self.match.player(1)
        return p.hold_piece if p.has_hold else None
    def get_debug_snapshot(self, player=0):
        dbg = self.debug1 if player == 0 else self.debug2
        return {
            "best_score": dbg.best_score,
            "think_ms": dbg.think_ms,
            "nodes": dbg.nodes,
            "expanded": dbg.expanded,
            "topk_scores": [sc for sc, _ in dbg.topk],
        }
