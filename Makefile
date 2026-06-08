CC       = cc
# OpenBLAS for VBL worker (fall back to Accelerate on macOS)
BLAS_LIBS = -lopenblas -L/opt/homebrew/opt/openblas/lib
BLAS_INC  = -I/opt/homebrew/opt/openblas/include
BLAS_HAS  = $(shell test -f /opt/homebrew/opt/openblas/include/cblas.h && echo 1 || echo 0)
ifeq ($(BLAS_HAS),0)
  BLAS_LIBS = -framework Accelerate
  BLAS_INC  =
endif

CFLAGS  += -O3 -ffast-math -Isrc -Iinclude -I. -Isrc/plugins/vbl $(BLAS_INC) $(shell pkg-config --cflags raylib) -DVXB_INTEGRATED
LDFLAGS += $(shell pkg-config --libs raylib) -lm -lz -lpthread $(BLAS_LIBS)

# static build: make STATIC=1
ifeq ($(STATIC),1)
  CFLAGS  += -DNDEBUG
  LDFLAGS  = -lm -lz -lpthread $(BLAS_LIBS)
  LDFLAGS += $(shell pkg-config --variable=libdir raylib)/libraylib.a
  LDFLAGS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
endif

# Full source list (will be added incrementally across tasks)
SRCS = src/main.c \
       src/load/loader.c \
       src/extract/extract.c \
       src/render/render.c \
       src/ui/sidebar.c \
       src/ui/panel.c \
       src/ui/viewport.c \
       src/input/hotkeys.c \
       src/save/screenshot.c \
       src/cli/cli.c \
       src/plugins/vbl/vbl_bridge.c \
       src/plugins/vbl/worker/parser.c \
       src/plugins/vbl/worker/types.c \
       src/plugins/vbl/worker/ops.c \
       src/plugins/vbl/worker/graph.c \
       src/plugins/vbl/worker/env.c \
       src/plugins/vbl/worker/pool.c

OBJS   = $(SRCS:src/%.c=build/%.o)
TARGET = build/voxelbase

# Only compile sources that exist on disk
EXISTING_OBJS = $(patsubst src/%.c,build/%.o,$(wildcard $(SRCS)))

all: $(TARGET)

$(TARGET): $(EXISTING_OBJS)
	@echo "  LINK  $@"
	@$(CC) $(CFLAGS) $(EXISTING_OBJS) $(LDFLAGS) -o $@

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

app: $(TARGET)
	@echo "→ Creating VoxelBase.app ..."
	@rm -rf build/VoxelBase.app build/logo.iconset
	@mkdir -p build/VoxelBase.app/Contents/MacOS
	@mkdir -p build/VoxelBase.app/Contents/Resources
	@mkdir -p build/logo.iconset
	@# generate .icns from logo.png
	@sips -z 16 16   assets/logo.png --out build/logo.iconset/icon_16x16.png >/dev/null 2>&1
	@sips -z 32 32   assets/logo.png --out build/logo.iconset/icon_32x32.png >/dev/null 2>&1
	@sips -z 128 128 assets/logo.png --out build/logo.iconset/icon_128x128.png >/dev/null 2>&1
	@sips -z 256 256 assets/logo.png --out build/logo.iconset/icon_256x256.png >/dev/null 2>&1
	@sips -z 512 512 assets/logo.png --out build/logo.iconset/icon_512x512.png >/dev/null 2>&1
	@iconutil -c icns build/logo.iconset -o build/VoxelBase.app/Contents/Resources/logo.icns 2>/dev/null
	@rm -rf build/logo.iconset
	@cp $(TARGET) build/VoxelBase.app/Contents/MacOS/
	@cp utils/Info.plist build/VoxelBase.app/Contents/
ifeq ($(STATIC),)
	@cp $(shell pkg-config --variable=libdir raylib)/libraylib.*.dylib build/VoxelBase.app/Contents/MacOS/ 2>/dev/null || true
	@install_name_tool -change @rpath/libraylib.5.5.0.dylib @executable_path/libraylib.5.5.0.dylib build/VoxelBase.app/Contents/MacOS/voxelbase 2>/dev/null || true
	@install_name_tool -change @rpath/libraylib.550.dylib @executable_path/libraylib.550.dylib build/VoxelBase.app/Contents/MacOS/voxelbase 2>/dev/null || true
endif
	@echo "  Done: build/VoxelBase.app"

install: app
	@rm -rf /Applications/VoxelBase.app
	@cp -R build/VoxelBase.app /Applications/
	@mkdir -p /usr/local/bin
	@echo '#!/bin/sh' > /usr/local/bin/voxelbase
	@echo 'exec /Applications/VoxelBase.app/Contents/MacOS/voxelbase "$$@"' >> /usr/local/bin/voxelbase
	@chmod 755 /usr/local/bin/voxelbase
	@echo "Installed → /Applications/VoxelBase.app"
	@echo "CLI     → /usr/local/bin/voxelbase"

.PHONY: all clean app install

# Standalone VBL worker shell (no GUI, for testing)
VBL_SRCS = src/plugins/vbl/worker/main.c \
           src/plugins/vbl/worker/parser.c \
           src/plugins/vbl/worker/types.c \
           src/plugins/vbl/worker/ops.c \
           src/plugins/vbl/worker/graph.c \
           src/plugins/vbl/worker/env.c \
           src/plugins/vbl/worker/pool.c
VBL_OBJS = $(VBL_SRCS:src/%.c=build/%.o)
VBL_CFLAGS = $(CFLAGS:-DVXB_INTEGRATED=) -DVXB_STANDALONE

vbl-worker: $(VBL_OBJS)
	@echo "  LINK  build/vbl-worker"
	@$(CC) $(VBL_CFLAGS) $(VBL_OBJS) $(LDFLAGS) -o build/vbl-worker
