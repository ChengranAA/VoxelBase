# VBL Worker — standalone build. Outputs to ../../build/
CC       = cc
CFLAGS   = -O2 -Wall -Wextra -std=c99
INCLUDES = -I../../../../include -I../../../.. -I/opt/homebrew/opt/openblas/include
LDLIBS   = -lm -lz -L/opt/homebrew/opt/openblas/lib -lopenblas

BUILD_DIR = ../../../../build/plugins/vbl/worker
WORKER_SRCS = main.c parser.c types.c ops.c graph.c env.c pool.c
WORKER_OBJS = $(WORKER_SRCS:%.c=$(BUILD_DIR)/vbl_%.o)
TARGET = $(BUILD_DIR)/vbl-worker

$(TARGET): $(WORKER_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(WORKER_OBJS) $(LDLIBS) -o $@

$(BUILD_DIR)/vbl_%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(WORKER_OBJS) $(TARGET)
