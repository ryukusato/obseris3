from setuptools import setup, Extension
import pybind11
from pathlib import Path

cpp_sources = [
    "pybind_module.cpp",
    "tetris_engine.cpp",
    "tetris_piece.cpp",
    "tetris_board.cpp",
    "tetris_rules.cpp",
    "tetris_step.cpp",
    "tetris_search.cpp",
    "tetris_state.cpp",
    "tetris_bag.cpp",
    "tetris_eval.cpp",
    "tetris_duel.cpp",
    "tetris_attack.cpp",
    "tetris_garbage.cpp",
    "tetris_gameover.cpp",
    "tetris_dag.cpp",
]

ext = Extension(
    "tetris_cpp",
    sources=cpp_sources,
    include_dirs=[
        pybind11.get_include(),
        str(Path(".").resolve()),
    ],
    language="c++",
    extra_compile_args=["-std=c++17", "-O3"],
)

setup(
    name="tetris_cpp",
    version="3.0",
    ext_modules=[ext],
)
