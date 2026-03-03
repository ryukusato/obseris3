# viewer/viewer_dag.py
import pygame

from adapter_dag import DuelGameAdapterDag
from renderer.board_renderer import BoardRenderer


def main():
    pygame.init()

    TOP_K = 5
    speed =  4
    paused = True

    # ★ DAG adapter を使う
    game = DuelGameAdapterDag(seed=1901, top_k=TOP_K)
    renderer = BoardRenderer(cell_size=28, top_k=TOP_K)

    clock = pygame.time.Clock()
    running = True
    winner_text = ""
    clear_flash = False

    while running:
        for e in pygame.event.get():
            if e.type == pygame.QUIT:
                running = False
            elif e.type == pygame.KEYDOWN:
                if e.key == pygame.K_ESCAPE:
                    running = False
                elif e.key == pygame.K_SPACE:
                    paused = not paused
                elif e.key == pygame.K_n and paused and not game.is_done():
                    game.step()
                elif e.key == pygame.K_UP or e.key == pygame.K_EQUALS:
                    speed = min(10, speed + 0.5)
                elif e.key == pygame.K_DOWN:
                    speed = max(0.5, speed - 0.5)

        if not paused and not game.is_done():
            game.step()
            if (game.debug1.lines_cleared > 0) or (game.debug2.lines_cleared > 0):
                clear_flash = True

        if game.is_done():
            winner_text = str(game.get_winner()).split(".")[-1]

        board1 = game.get_board1()
        board2 = game.get_board2()

        # ★ viewer は Adapter API のみを見る
        next1 = game.get_next1(5)
        next2 = game.get_next2(5) 
        hold1 = game.get_hold1()
        hold2 = game.get_hold2()

        renderer.draw_duel(
            board1=board1,
            board2=board2,
            info1=game.get_info1(),
            info2=game.get_info2(),
            topk1=game.get_topk1(),
            topk2=game.get_topk2(),
            landing1=game.get_last_landing1(),
            landing2=game.get_last_landing2(),
            board_before1=game.get_board_before1(),
            board_before2=game.get_board_before2(),
            clear_flash=clear_flash,
            paused=paused,
            speed=speed,
            winner_text=winner_text,
            next1=next1,
            next2=next2,
            hold1=hold1,
            hold2=hold2,
        )

        clock.tick(30 if paused else speed)

    pygame.quit()


if __name__ == "__main__":
    main()
