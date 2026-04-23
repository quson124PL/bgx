linux: bgx.c
	gcc -Os -lm -Wall -Wextra -static-libgcc bgx.c -o bgx
	chmod +x ./bgx

win32: bgx.c
	i686-w64-mingw32-gcc -Os -lm -Wall -Wextra -static -static-libgcc bgx.c -o static_mingw_win32_bgx.exe

win64: bgx.c
	x86_64-w64-mingw32-gcc -Os -lm -Wall -Wextra -static -static-libgcc bgx.c -o static_mingw_win64_bgx.exe

all: linux win32 win64