project('tetris', 'cpp', version: 'v1.0.0', default_options: ['cpp_std=c++17'], meson_version: '>=1.1')

gtk_dep = dependency('gtk+-2.0')

add_project_arguments('-Wno-deprecated-declarations', language: 'cpp')

sources = files(
        'src/main.cpp',
        'src/components/tetris_board.cpp',
        'src/components/tetris_board.hpp',
        'src/components/tetris_game.cpp',
        'src/include/tetris_game.hpp'
)

include_dirs = include_directories(
        './src/include/'
)

executable('tetris', sources, include_directories: include_dirs, dependencies: [gtk_dep], cpp_args: '-static-libstdc++', link_args: '-static-libstdc++')
