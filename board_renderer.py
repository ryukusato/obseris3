# viewer/renderer/board_renderer.py
import pygame
import tetris_cpp as tc

PIECE_COLORS = {
    tc.PieceType.I: (0, 240, 240),
    tc.PieceType.O: (240, 240, 0),
    tc.PieceType.T: (160, 0, 240),
    tc.PieceType.S: (0, 240, 0),
    tc.PieceType.Z: (240, 0, 0),
    tc.PieceType.J: (0, 0, 240),
    tc.PieceType.L: (240, 160, 0),
}

class BoardRenderer:
    def __init__(self, cell_size=28, top_k=5):
        self.cell = cell_size
        self.board_w = 10
        self.board_h = 21
        self.top_k = top_k
        self.board_gap = 3 * self.cell   # ★ ここ（調整しやすい）


        # ---- layout sizes ----
        self.info_w = 10 * self.cell          # left / right info tower
        self.board_w_px = self.board_w * self.cell
        self.bottom_h = 2 * self.cell
        

        self.width = self.info_w * 2 + self.board_w_px * 2 + self.board_gap

        self.height = self.board_h * self.cell + self.bottom_h

        self.screen = pygame.display.set_mode((self.width, self.height))
        pygame.display.set_caption("Tetris AI Duel Viewer")

        # ---- fonts ----
        self.font = pygame.font.SysFont("consolas", 18)
        self.small = pygame.font.SysFont("consolas", 14)
        self.tiny = pygame.font.SysFont("consolas", 12)

        # ---- colors ----
        self.bg = (18, 18, 18)
        self.grid = (45, 45, 45)
        self.text = (235, 235, 235)
        self.dim = (150, 150, 150)
        self.block = (120, 120, 120)

    # =========================================================
    # public
    # =========================================================
    def draw_duel(
        self,
        board1, board2,
        info1, info2,
        topk1, topk2,
        landing1, landing2,
        board_before1=None, board_before2=None,
        clear_flash=False,
        paused=False, speed=10, winner_text="",
        next1=None, next2=None,
        hold1=None, hold2=None,
    ):
        self.screen.fill(self.bg)

        # ---- x offsets ----
        ox_info1  = 0
        ox_board1 = self.info_w
        ox_board2 = self.info_w + self.board_w_px + self.board_gap
        ox_info2  = self.info_w + self.board_w_px * 2 + self.board_gap


        oy = 0

        # ---- info towers ----
        self._draw_info_tower(
            ox_info1, info1, landing1, topk1, next1, hold1, "PLAYER 1"
        )
        self._draw_info_tower(
            ox_info2, info2, landing2, topk2, next2, hold2, "PLAYER 2"
        )




        # ---- boards ----
        if clear_flash and board_before1 is not None:
            self._draw_board(board_before1, ox_board1, oy)
        else:
            self._draw_board(board1, ox_board1, oy)

        if clear_flash and board_before2 is not None:
            self._draw_board(board_before2, ox_board2, oy)
        else:
            self._draw_board(board2, ox_board2, oy)



        # ---- ghosts ----
        self._draw_topk_ghosts(topk1, ox_board1, oy)
        self._draw_topk_ghosts(topk2, ox_board2, oy)

        self._draw_landing_fill(landing1, ox_board1, oy)
        self._draw_landing_fill(landing2, ox_board2, oy)

        if clear_flash:

            self._draw_clear_flash(board_before1, board1, ox_board1, oy)
            self._draw_clear_flash(board_before2, board2, ox_board2, oy)
        
        self._draw_grid(ox_board1, oy)
        self._draw_grid(ox_board2, oy)

    
        self._draw_landing_outline(landing1, ox_board1, oy)
        self._draw_landing_outline(landing2, ox_board2, oy)

    


        # ---- bottom ----
        self._draw_bottom(paused, speed, winner_text)

        pygame.display.flip()

    # =========================================================
    # info tower
    # =========================================================
    def _draw_info_tower(self, ox, info, landing, topk, nextp, holdp, label):
        y = 8
        col_gap = self.cell * 2
        col_w = self.cell * 3

        # --- header ---
        self.screen.blit(self.font.render(label, True, self.text), (ox + 8, y))
        y += 22

        eval_txt = f"{info['mode']} | {info['eval']/1000:.3f}k"
        self.screen.blit(self.small.render(eval_txt, True, self.dim), (ox + 8, y))
        y += 24

        eval_time_txt = f"Think: {info['think_ms']:.1f} ms | Turn: {info['turn']}"
        self.screen.blit(self.tiny.render(eval_time_txt, True, self.dim), (ox + 8, y))
        y += 20

        # --- clear / eval info ---
        used_h = self._draw_player_info(ox + 8, y, info, landing, topk)
        y += used_h + 12

        # --- two columns setup ---
        left_x  = ox + 8
        right_x = ox + 8 + col_w + col_gap

        # Playerによって左右の内容を入れ替え
        if "1" in label:  # PLAYER 1 (Left: NEXT, Right: HOLD)
            left_content, left_lbl  = nextp, "NEXT"
            right_content, right_lbl = holdp, "HOLD"
        else:             # PLAYER 2 (Left: HOLD, Right: NEXT)
            left_content, left_lbl  = holdp, "HOLD"
            right_content, right_lbl = nextp, "NEXT"

        # --- LEFT COLUMN ---
        self.screen.blit(self.small.render(left_lbl, True, self.text), (left_x, y))
        if left_lbl == "NEXT" and isinstance(left_content, list):
            # NEXTリストを縦に並べる
            for i, p in enumerate(left_content[:5]):
                # ミノの間隔を self.cell * 2.5 程度にすると重ならず綺麗です
                self._draw_mini_piece(p, left_x, y + 20 + (i * int(self.cell * 2.5)))
        else:
            # HOLD (または単体のNEXT) を描画
            self._draw_mini_piece(left_content, left_x, y + 20)

        # --- RIGHT COLUMN ---
        self.screen.blit(self.small.render(right_lbl, True, self.text), (right_x, y))
        if right_lbl == "NEXT" and isinstance(right_content, list):
            # NEXTリストを縦に並べる
            for i, p in enumerate(right_content[:5]):
                self._draw_mini_piece(p, right_x, y + 20 + (i * int(self.cell * 2.5)))
        else:
            # HOLD (または単体のNEXT) を描画
            self._draw_mini_piece(right_content, right_x, y + 20)

    # =========================================================
    # bottom
    # =========================================================
    def _draw_bottom(self, paused, speed, winner):
        y = self.board_h * self.cell + 6
        msg = (
            f"SPACE:Pause   N:Step   "
            f"+/- or ↑↓:Speed({speed})   "
            f"H/J:Heuristic   C/K:CNN   ESC:Quit"
        )

        if paused:
            msg += "   [PAUSED]"
        if winner:
            msg += f"   Winner:{winner}"

        self.screen.blit(
            self.small.render(msg, True, self.text),
            (8, y)
        )


    # =========================================================
    # board drawing
    # =========================================================
    def _draw_board(self, board, ox, oy):
        for y in range(self.board_h):
            for x in range(self.board_w):
                if int(board[y][x]):
                    pygame.draw.rect(
                        self.screen,
                        self.block,
                        (
                            ox + x*self.cell,
                            oy + (self.board_h-1-y)*self.cell,
                            self.cell-1,
                            self.cell-1,
                        )
                    )

    def _draw_grid(self, ox, oy):
        bw = self.board_w * self.cell
        bh = self.board_h * self.cell
        for x in range(self.board_w + 1):
            px = ox + x * self.cell
            pygame.draw.line(self.screen, self.grid, (px, oy), (px, oy + bh))
        for y in range(self.board_h + 1):
            py = oy + y * self.cell
            if y == 1 : #太く
                pygame.draw.line(self.screen, (200,200,200), (ox, py), (ox + bw, py), 2)
            else:
                pygame.draw.line(self.screen, self.grid, (ox, py), (ox + bw, py))

    # =========================================================
    # pieces
    # =========================================================
    def _draw_topk_ghosts(self, topk, ox, oy):
        for i, (_, l) in enumerate(topk[: self.top_k]):
            alpha = max(40, 120 - i*20)
            self._draw_ghost(l, ox, oy, alpha)

    def _draw_ghost(self, landing, ox, oy, alpha):
        if landing is None:
            return
        try:
            cells = tc.get_shape_cells(landing.piece, int(landing.final_rot))
        except Exception:
            return

        color = PIECE_COLORS.get(landing.piece, (200,200,200))
        surf = pygame.Surface((self.cell, self.cell), pygame.SRCALPHA)
        surf.fill((*color, alpha))
        for dx, dy in cells:
            x = landing.final_x + dx
            y = landing.final_y + dy
            if 0 <= x < self.board_w and 0 <= y < self.board_h:
                self.screen.blit(
                    surf,
                    (ox + x*self.cell,
                     oy + (self.board_h-1-y)*self.cell)
                )
    
    def _draw_landing_outline(self, landing, ox, oy):
        if landing is None:
            return
        try:
            cells = tc.get_shape_cells(landing.piece, int(landing.final_rot))
        except Exception:
            return
        color = PIECE_COLORS.get(landing.piece, (255,255,255))
        for dx, dy in cells:
            x = landing.final_x + dx
            y = landing.final_y + dy
            if 0 <= x < self.board_w and 0 <= y < self.board_h:
                pygame.draw.rect(
                    self.screen,
                    color,
                    (
                        ox + x*self.cell,
                        oy + (self.board_h-1-y)*self.cell,
                        self.cell-1,
                        self.cell-1,
                    ),
                    3
                )

    def _draw_landing_fill(self, landing, ox, oy):
        if landing is None:
            return
        try:
            cells = tc.get_shape_cells(landing.piece, int(landing.final_rot))
        except Exception:
            return

        color = PIECE_COLORS.get(landing.piece, (200,200,200))
        for dx, dy in cells:
            x = landing.final_x + dx
            y = landing.final_y + dy
            if 0 <= x < self.board_w and 0 <= y < self.board_h:
                pygame.draw.rect(
                    self.screen,
                    color,
                    (
                        ox + x*self.cell,
                        oy + (self.board_h-1-y)*self.cell,
                        self.cell-1,
                        self.cell-1,
                    )
                )

    # =========================================================
    # clear
    # =========================================================
    def _draw_clear_flash(self, before, after, ox, oy):
        bright = (245, 245, 245)

        for y in range(self.board_h):
            # beforeで完全に埋まっていた行だけを見る
            if all(before[y][x] for x in range(self.board_w)):
                for x in range(self.board_w):
                    pygame.draw.rect(
                        self.screen,
                        bright,
                        (
                            ox + x * self.cell,
                            oy + (self.board_h - 1 - y) * self.cell,
                            self.cell - 1,
                            self.cell - 1,
                        )
                    )





    # =========================================================
    # mini piece
    # =========================================================
    def _draw_mini_piece(self, piece, ox, oy):
        if piece is None:
            return
        try:
            cells = tc.get_shape_cells(piece, 0)
        except Exception:
            return
        color = PIECE_COLORS.get(piece, (200,200,200))
        xs = [dx for dx, dy in cells]
        ys = [dy for dx, dy in cells]
        minx, miny = min(xs), min(ys)
        for dx, dy in cells:
            x = dx - minx
            y = dy - miny
            pygame.draw.rect(
                self.screen,
                color,
                (
                    ox + x*self.cell,
                    oy + (3-y)*self.cell,
                    self.cell-2,
                    self.cell-2,
                )
            )
    def _draw_player_info(self, x, y, info, landing, topk):
        y0 = y

        def draw(text, dy=18, color=None, font=None):
            nonlocal y
            if color is None:
                color = self.text
            if font is None:
                font = self.font
            self.screen.blit(
                font.render(text, True, color),
                (x, y)
            )
            y += dy

        # --- clear type（やや大きめ）---
        draw(
            "Clear : " + clear_text(landing),
            font=self.small
        )

        # --- best eval（小さく）---
        best = info["eval"]
        draw(
            f"Best  : {best:.5e}",
            color=(255,220,160),
            font=self.tiny
        )

        # --- top-k evals（さらに小さく）---
        if topk:
            if abs(best) >= 1000:
                vals = [f"{v/1000:.3f}k" for v, _ in topk[:3]]
            else:
                vals = [f"{v:.3f}" for v, _ in topk[:3]]

            draw(
                "TopK  : " + " | ".join(vals),
                dy=16,
                font=self.tiny,
                color=self.dim
            )
        
        draw(
             f"beam'ply':{info['beam']['ply']}'width':{info['beam']['width']}w" if info.get('beam') else "No Beam",
             dy = 16,
             font = self.tiny,
             color = self.dim
        )
        

        return y - y0


def clear_text(landing):
    if landing is None:
        return "—"

    if landing.lines_cleared == 0:
        return "No Clear"

    kind = landing.kind

    # 表示名（enum名そのまま）
    name = kind.name

    # B2B 表示は限定
    if landing.back_to_back and kind in {
        tc.ClearKind.Tspin1,
        tc.ClearKind.Tspin2,
        tc.ClearKind.Tspin3,
        tc.ClearKind.Clear4,
        tc.ClearKind.MiniTspin1,
        tc.ClearKind.MiniTspin2,
    }:
        name += " (B2B)"

    if landing.perfect_clear:
        name += " (PC)"
    if landing.combo >= 2:
        name += f" REN{landing.combo - 1}"
    return name

