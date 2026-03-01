CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c11
FRAMEWORKS = -framework AudioToolbox -framework CoreFoundation -framework Accelerate

TARGET = hzview
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(FRAMEWORKS)

clean:
	rm -f $(TARGET)
