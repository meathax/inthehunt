#include "../src/world_timing_options.h"
#include <stdio.h>
#include <string.h>
int main(void){WorldTimingMode mode=WORLD_TIMING_ARCADE;if(!world_timing_mode_parse("remove-slowdown",&mode)||mode!=WORLD_TIMING_REMOVE_SLOWDOWN)return 1;if(world_timing_mode_parse("unlimited",&mode)||mode!=WORLD_TIMING_REMOVE_SLOWDOWN)return 2;if(!world_timing_mode_parse("Arcade",&mode)||mode!=WORLD_TIMING_ARCADE)return 3;if(strcmp(world_timing_mode_name(mode),"arcade"))return 4;if(world_timing_mode_parse(NULL,&mode)||world_timing_mode_parse("arcade",NULL))return 5;puts("timing mode parsing and invalid-value preservation passed");return 0;}
