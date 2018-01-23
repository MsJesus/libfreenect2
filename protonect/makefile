TARGET = protonect
CC = g++

CFLAGS = -g -Wall -Os -std=gnu++11
CFLAGS += -I..
CFLAGS += -I./../libfreenect2
CFLAGS += -I./../include
#CFLAGS += -I./../external/libusb
#CFLAGS += -I./../external/turbojpeg

LDFLAGS = -Wall
LDFLAGS += -L/usr/local/opt/libjpeg-turbo/lib
LDFLAGS += -L/usr/local/opt/libusb/lib
LDFLAGS += -lfreenect2
LDFLAGS += -lturbojpeg
LDFLAGS += -lusb

.PHONY: default all clean $(TARGET)

default: $(TARGET)
all: default


BUILD_DIR = ../build
OBJ_DIR = $(BUILD_DIR)/obj/$(TARGET)
BIN_DIR = $(BUILD_DIR)/bin

LDFLAGS += -L$(BIN_DIR)

HEADERS = $(shell find . -type f -name '*.h' -not -name 'viewer.*' -not -name 'flextGL.*')
SOURCES = $(shell find . -type f -name '*.cpp' -not -name 'viewer.*' -not -name 'flextGL.*')
OBJECTS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(SOURCES))
EXECUTABLE = $(BIN_DIR)/$(TARGET)

#LIB_FILES = $(shell find ../libfreenect2 -type f)

$(BIN_DIR)/libfreenect2.a:
	$(MAKE) -C ../libfreenect2

directories:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

$(OBJ_DIR)/%.o: %.cpp $(HEADERS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(EXECUTABLE): $(OBJECTS) $(BIN_DIR)/libfreenect2.a
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(TARGET): directories $(EXECUTABLE)

clean:
	rm -rf $(BUILD_DIR)  
