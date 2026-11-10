# Linux developer build of the reference checks (never shipped).
#   make -f tools/ref/build.mk        from the project root
CC ?= gcc
CFLAGS ?= -std=c99 -O2 -g -Wall -Wextra
CORE = dispatch actors_1 actors_2 actors_3 actors_4 actors_5 actors_6 stage1 stage2 stage3 stage4 stage5 stage6 common gaps credits coop explore1 explore2 \
       title title_master gameover enemy_target stage fire camera torpedo contact \
       terrain death score candidates player frame frame_video palette
CORE_SRC = $(patsubst %,src/world_%.c,$(CORE))
OUT = .local/bin
all: $(OUT)/check_replay $(OUT)/native_run
$(OUT)/check_replay: tools/ref/check_replay.c $(CORE_SRC) $(wildcard src/*.h)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ tools/ref/check_replay.c $(CORE_SRC)
HOST = boot video ga20 audio_driver audio_notes
HOST_SRC = $(patsubst %,src/world_%.c,$(HOST))
$(OUT)/native_run: tools/ref/native_run.c $(CORE_SRC) $(HOST_SRC) src/world_ym.cpp $(wildcard src/*.h)
	@mkdir -p $(OUT)/obj
	for f in $(CORE_SRC) $(HOST_SRC) tools/ref/native_run.c; do \
	  $(CC) $(CFLAGS) -c $$f -o $(OUT)/obj/$$(basename $$f .c).o || exit 1; done
	$(CXX) -std=c++17 -O2 -w -c src/world_ym.cpp -o $(OUT)/obj/ym.o
	$(CXX) -std=c++17 -O2 -w -c src/third_party/ymfm/ymfm_opm.cpp -o $(OUT)/obj/opm.o
	$(CXX) -o $@ $(OUT)/obj/*.o
