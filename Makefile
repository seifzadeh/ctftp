CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=gnu99 -pthread
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj

SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/config.c \
       $(SRC_DIR)/logger.c \
       $(SRC_DIR)/util.c \
       $(SRC_DIR)/events.c \
       $(SRC_DIR)/tftp.c

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TARGET = ctftp

.PHONY: all clean static

all: $(TARGET)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# Static build (may require static glibc on your system)
static: CFLAGS += -static
static: LDFLAGS += -static
static: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(TARGET)-static

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TARGET)-static
