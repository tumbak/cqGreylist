all:
	gcc -Wall -Wextra -ansi -Wno-implicit-function-declaration -pedantic cqgreylist.c -o cqgreylist
	strip cqgreylist

dev:
	gcc -DDEBUG -g -Wall cqgreylist.c -o cqgreylist

clean:
	rm -f cqgreylist
