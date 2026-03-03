#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "tetris_state.h"
#include "tetris_eval.h"
#include "tetris_gameover.h"
#include "tetris_duel.h"
#include "tetris_engine.h"
#include "tetris_dag.h"
namespace py = pybind11;

static std::vector<std::vector<int>>
bitboard_to_vector(const BitBoard &bb)
{
    std::vector<std::vector<int>> out(
        TOTAL_BOARD_HEIGHT,
        std::vector<int>(BOARD_WIDTH, 0));

    for (int y = 0; y < TOTAL_BOARD_HEIGHT; ++y)
        for (int x = 0; x < BOARD_WIDTH; ++x)
            out[y][x] = (bb.row[y] >> x) & 1;

    return out;
}

PYBIND11_MODULE(tetris_cpp, m) {

    py::enum_<PieceType>(m, "PieceType")
        .value("I", PieceType::I)
        .value("O", PieceType::O)
        .value("T", PieceType::T)
        .value("S", PieceType::S)
        .value("Z", PieceType::Z)
        .value("J", PieceType::J)
        .value("L", PieceType::L)
        .export_values();

    py::enum_<ClearKind>(m, "ClearKind")
        .value("None", ClearKind::None)
        .value("Clear1", ClearKind::Clear1)
        .value("Clear2", ClearKind::Clear2)
        .value("Clear3", ClearKind::Clear3)
        .value("Clear4", ClearKind::Clear4)
        .value("Tspin1", ClearKind::Tspin1)
        .value("Tspin2", ClearKind::Tspin2)
        .value("Tspin3", ClearKind::Tspin3)
        .value("MiniTspin1", ClearKind::MiniTspin1)
        .value("MiniTspin2", ClearKind::MiniTspin2)
        .export_values();

    
    py::class_<Landing>(m, "Landing")
        .def_property_readonly("board_after",
                               [](const Landing &l)
                               {
                                   return bitboard_to_vector(l.bb_after);
                               })
        .def_readonly("lines_cleared", &Landing::lines_cleared)
        .def_readonly("perfect_clear", &Landing::perfect_clear)
        .def_readonly("combo", &Landing::combo)
        .def_readonly("used_hold", &Landing::used_hold)
        .def_readonly("piece", &Landing::piece)
        .def_readonly("kind", &Landing::kind)
        .def_readonly("path", &Landing::path)
        .def_readonly("attack", &Landing::attack)
        .def_readonly("back_to_back", &Landing::back_to_back)
        .def_readonly("used_t_piece", &Landing::used_t_piece)
        .def_readonly("final_x", &Landing::final_x)
        .def_readonly("final_y", &Landing::final_y)
        .def_readonly("final_rot", &Landing::final_rot);

    py::class_<LandingSearch>(m, "LandingSearch")
        // ---- 入力として見る部分 ----
        .def_readonly("piece", &LandingSearch::piece)
        .def_readonly("final_x", &LandingSearch::final_x)
        .def_readonly("final_y", &LandingSearch::final_y)
        .def_readonly("final_rot", &LandingSearch::final_rot)
        .def_readonly("used_hold", &LandingSearch::used_hold)
        .def_readonly("perfect_clear", &LandingSearch::perfect_clear)
        .def_readonly("lines_cleared", &LandingSearch::lines_cleared)
        .def_readonly("attack", &LandingSearch::attack)
        .def_readonly("kind", &LandingSearch::kind)

        // ---- 実行に必要な after 状態 ----
        .def_readonly("bb_after", &LandingSearch::bb_after)

        .def_readonly("combo", &LandingSearch::combo_after)
        .def_readonly("back_to_back", &LandingSearch::b2b_after)

        .def_readonly("has_hold_after", &LandingSearch::has_hold_after)
        .def_readonly("hold_piece_after", &LandingSearch::hold_piece_after)

        .def_readonly("current_after", &LandingSearch::current_after)
        .def_readonly("bag_index_after", &LandingSearch::bag_index_after)

        .def_readonly("spawn_x_after", &LandingSearch::spawn_x_after)
        .def_readonly("spawn_y_after", &LandingSearch::spawn_y_after)

        .def_readonly("dead_after", &LandingSearch::dead_after)

        .def_readonly("used_hold_this_turn_after",
                      &LandingSearch::used_hold_this_turn_after);

    py::class_<GameState>(m, "GameState")
        .def_property_readonly("board",
                               [](const GameState &s)
                               {
                                   return bitboard_to_vector(s.bb);
                               })
        .def_readonly("current", &GameState::current)
        .def_readonly("combo", &GameState::combo)
        .def_readonly("back_to_back", &GameState::back_to_back)
        .def_readonly("has_hold", &GameState::has_hold)
        .def_readonly("hold_piece", &GameState::hold_piece)
        .def_readonly("pending_garbage", &GameState::pending_garbage)
        .def_readonly("spawn_x", &GameState::spawn_x)
        .def_readonly("spawn_y", &GameState::spawn_y)
        .def_readonly("bag_index", &GameState::bag_index)
        .def_readonly("dead", &GameState::dead);

    py::class_<MatchState>(m, "MatchState")
        .def(py::init<unsigned int, unsigned int>())
        .def_readwrite("turn", &MatchState::turn)
        .def("player", [](MatchState &m, int i) -> GameState &
             {
        if (i < 0 || i >= 2)
            throw std::out_of_range("player index");
        return m.players[i]; }, py::return_value_policy::reference_internal);

    py::class_<DagConfig>(m, "DagConfig")
        .def(py::init<>())
        .def_readwrite("iterations", &DagConfig::iterations)
        .def_readwrite("max_depth", &DagConfig::max_depth)
        .def_readwrite("max_nodes", &DagConfig::max_nodes)
        .def_readwrite("ucb_c", &DagConfig::ucb_c)
        .def_readwrite("top_k", &DagConfig::top_k)
        .def_readwrite("bag_probe", &DagConfig::bag_probe)
        .def_readwrite("use_landing_reward", &DagConfig::use_landing_reward);

    py::class_<DagTiming>(m, "DagTiming")
        .def(py::init<>())
        .def_readonly("iters", &DagTiming::iters)

        .def_readonly("enum_ns", &DagTiming::enum_ns)
        .def_readonly("leaf_ns", &DagTiming::leaf_ns)
        .def_readonly("expand_ns", &DagTiming::expand_ns)
        .def_readonly("backprop_ns", &DagTiming::backprop_ns)

        .def_readonly("max_enum_ns", &DagTiming::max_enum_ns)
        .def_readonly("max_backprop_ns", &DagTiming::max_backprop_ns);

    py::class_<DagCandidate>(m, "DagCandidate")
        .def_readonly("move", &DagCandidate::move)
        .def_readonly("score", &DagCandidate::score);

    py::class_<DagResult>(m, "DagResult")
        .def_readonly("best", &DagResult::best)
        .def_readonly("best_score", &DagResult::best_score)
        .def_readonly("topk", &DagResult::topk)
        .def_readonly("nodes", &DagResult::nodes)
        .def_readonly("think_ms", &DagResult::think_ms)
        .def_readonly("expanded", &DagResult::expanded)
        .def_readonly("timing", &DagResult::timing);

    m.def("dag_search", &dag_search,
          py::arg("state"),
          py::arg("weights"),
          py::arg("config"));

    m.def("play_move", &play_move);
    m.def(
        "play_move_search",
        &play_move_search,
        py::arg("match"),
        py::arg("landing_search"));
    m.def("legal_moves", [](const GameState &s)
          { return legal_moves(s); });
    m.def(
        "simulate_move",
        &simulate_move,
        py::arg("state"),
        py::arg("landing"),
        R"pbdoc(
    Simulate a move without affecting the real match state.

    Returns:
        GameState: next state after applying the landing.
    )pbdoc");
    m.def("next_pieces",
          &next_pieces,
          py::arg("state"),
          py::arg("n"));

    m.def("apply_move", [](const GameState &s, const Landing &l)
          { return apply_move(s, l); });

    m.def("evaluate_landing", [](const Landing &l)
          {
        EvalWeights w;              // CC デフォルト重み
        return evaluate_landing(l, w); });

    m.def("judge_winner",
          [](const GameState &p0, const GameState &p1)
          {
              return judge_winner(
                  p0.bb, p0.current,
                  p1.bb, p1.current);
          });

    m.def("is_dead_state", &is_dead_state);
    m.def("start_turn", py::overload_cast<MatchState &>(&start_turn));

    // Action enum を Python に公開
    py::enum_<Action>(m, "Action")
        .value("None", Action::None)
        .value("MoveLeft", Action::MoveLeft)
        .value("MoveRight", Action::MoveRight)
        .value("SoftDrop", Action::SoftDrop)
        .value("HardDrop", Action::HardDrop)
        .value("RotateCW", Action::RotateCW)
        .value("RotateCCW", Action::RotateCCW)
        .value("Hold", Action::Hold)
        .export_values();

    m.def("judge_winner", &judge_winner,
          py::arg("board1"), py::arg("piece1"),
          py::arg("board2"), py::arg("piece2"));

    py::enum_<Winner>(m, "Winner")
        .value("Undecided", Winner::None)
        .value("Player1", Winner::Player1)
        .value("Player2", Winner::Player2)
        .value("Draw", Winner::Draw);

    m.def("get_shape_cells",
          [](PieceType p, int rot)
          {
              const Coords &c = get_shape(p, rot);
              std::vector<std::pair<int, int>> out(c.begin(), c.end());
              return out;
          });
    m.def(
        "engine_winner",
        &engine_winner,
        py::arg("match"),
        "Return winner based on dead state of players");

    py::class_<EvalWeights>(m, "EvalWeights")
        .def(py::init<>())

        // ---- 基本形状 ----
        .def_readwrite("height", &EvalWeights::height)
        .def_readwrite("bumpiness", &EvalWeights::bumpiness)
        .def_readwrite("bumpiness_sq", &EvalWeights::bumpiness_sq)

        .def_readwrite("row_trans", &EvalWeights::row_trans)
        .def_readwrite("covered", &EvalWeights::covered)
        .def_readwrite("covered_sq", &EvalWeights::covered_sq)

        .def_readwrite("cavity_cells", &EvalWeights::cavity_cells)
        .def_readwrite("cavity_cells_sq", &EvalWeights::cavity_cells_sq)
        .def_readwrite("overhang_cells", &EvalWeights::overhang_cells)
        .def_readwrite("overhang_cells_sq", &EvalWeights::overhang_cells_sq)

        .def_readwrite("top_half", &EvalWeights::top_half)
        .def_readwrite("top_quarter", &EvalWeights::top_quarter)

        // ---- well ----
        .def_readwrite("well_depth", &EvalWeights::well_depth)
        .def_readwrite("max_well_cap", &EvalWeights::max_well_cap)
        .def_readwrite("well_column", &EvalWeights::well_column)

        // ---- B2B / combo ----
        .def_readwrite("b2b_clear", &EvalWeights::b2b_clear)
        .def_readwrite("combo_bonus", &EvalWeights::combo_bonus)

        // ---- T-spin ----
        .def_readwrite("tspin1", &EvalWeights::tspin1)
        .def_readwrite("tspin2", &EvalWeights::tspin2)
        .def_readwrite("tspin3", &EvalWeights::tspin3)
        .def_readwrite("mini_tspin1", &EvalWeights::mini_tspin1)
        .def_readwrite("mini_tspin2", &EvalWeights::mini_tspin2)

        // ---- T-slot ----
        .def_readwrite("tslot", &EvalWeights::tslot)

        // ---- line clear ----
        .def_readwrite("clear1", &EvalWeights::clear1)
        .def_readwrite("clear2", &EvalWeights::clear2)
        .def_readwrite("clear3", &EvalWeights::clear3)
        .def_readwrite("clear4", &EvalWeights::clear4)

        // ---- special ----
        .def_readwrite("perfect_clear", &EvalWeights::perfect_clear)
        .def_readwrite("wasted_t", &EvalWeights::wasted_t);
}
