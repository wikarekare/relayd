all: relayd
	
relayd: relayd.c
	cc -o relayd relayd.c

clean:
	rm -f *.o *.a relayd

install: all
	install relayd /usr/local/sbin

