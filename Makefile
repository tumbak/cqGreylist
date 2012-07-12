all:
	gcc -Wall -Wextra -pedantic cqgreylist.c -o cqgreylist
	strip cqgreylist

dev:
	gcc -DDEBUG -g -Wall cqgreylist.c -o cqgreylist

clean:
	rm -f cqgreylist
