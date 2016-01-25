CFLAGS += -std=c99 -g -O3 -Wall #-Werror
LDFLAGS += -lmosquitto

# Uncomment this to print out debugging info.
#CFLAGS += -DDEBUG

PROJECT=client

all: ${PROJECT}

client: client.o
	cc client.o ${LDFLAGS} -o client

client.o: Makefile

clean:
	rm -rf *.o ${PROJECT}
