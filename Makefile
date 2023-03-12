all: httpd

httpd: httpd.c
	gcc -W -Wall -lpthread -o httpd httpd.c

httpd-debug: httpd.c
	gcc -W -Wall -DDEBUG -lpthread -o httpd httpd.c

clean:
	rm httpd
