#cc -DMYTEST sdl2.c `sdl2-config --libs` -lSDL2_gfx -o testbin
cc -O2 rubishgraphics.c `sdl2-config --libs` -lSDL2_gfx -o rubishgfx