all:
	gcc -Os -static-libgcc bgx.c -o bgx
	chmod +x ./bgx