client: client.c
	gcc -pthread -ansi -pedantic -Wall -std=c17 -o client client.c
clean:
	rm -f client *.o
