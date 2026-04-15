linux: bgx.c
	gcc -Os -Wall -Wextra -static-libgcc bgx.c -o bgx
	chmod +x ./bgx

win32: bgx.c
	i686-w64-mingw32-gcc -Os -Wall -Wextra -static -static-libgcc bgx.c -o bgx32.exe

win64: bgx.c
	x86_64-w64-mingw32-gcc -Os -Wall -Wextra -static -static-libgcc bgx.c -o bgx64.exe

all: linux win32 win64