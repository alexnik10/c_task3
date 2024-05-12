all: myinit

myinit: myinit.c
	gcc -Wall -o myinit myinit.c

clean:
	rm -f myinit