CC = gcc
CFLAGS = -O -g
TARGET = testlib

objects = main.o bitmap.o debug.o hash.o hex_dump.o list.o

%.o : %.c
				$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET) : $(objects)
				$(CC) $(CFLAGS) -o $(TARGET) $(objects)

.PHONY : clean
clean:
				rm $(TARGET) $(objects)