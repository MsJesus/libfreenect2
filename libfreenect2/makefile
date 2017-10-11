TARGET = libfreenect2
CC = g++
CFLAGS = -g -Wall -Os -std=gnu++11 -I.. -I../external/libusb -I../external/turbojpeg
LDFLAGS = -Wall -ljpeg -lusb


.PHONY: default all clean $(TARGET)


default: $(TARGET)
all: default


BUILD_DIR = ../build
OBJ_DIR = $(BUILD_DIR)/obj/$(TARGET)
BIN_DIR = $(BUILD_DIR)/bin

HEADERS = $(shell find . -type f -name '*.h')
SOURCES = $(shell find . -type f -name '*.cpp')
OBJECTS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(SOURCES))
EXECUTABLE = $(BIN_DIR)/lib$(TARGET).dylib

directories:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

$(OBJ_DIR)/%.o: %.cpp $(HEADERS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(EXECUTABLE): $(OBJECTS)
	ar rcs $@ $(OBJECTS)

$(TARGET): directories $(EXECUTABLE)

clean:
	rm -rf $(BUILD_DIR)  
