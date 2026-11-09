# Linux developer build of the reference checks (never shipped).
#   make -f tools/ref/Makefile        from the project root
CC ?= gcc
CFLAGS ?= -std=c99 -O2 -g -Wall -Wextra
CORE = dispatch actors_1 actors_2 actors_3 actors_4 actors_5 actors_6 stage1 stage2 stage3 stage4 stage5 stage6 common \
       title title_master gameover enemy_target stage fire camera torpedo contact \
       terrain death score candidates player frame frame_video palette
CORE_SRC = $(patsubst %,src/world_%.c,$(CORE))
OUT = .local/bin
all: $(OUT)/check_replay
$(OUT)/check_replay: tools/ref/check_replay.c $(CORE_SRC) $(wildcard src/*.h)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ tools/ref/check_replay.c $(CORE_SRC)
