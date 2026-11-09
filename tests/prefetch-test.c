#include "../src/world_cycle_costs.h"
#include <stdint.h>
#include <stdio.h>
static uint32_t seed=17;
static uint32_t next(void){seed^=seed<<13;seed^=seed>>17;return seed^=seed<<5;}
int main(void){
 WorldCyclePrefetch own={0};int reference_count=0;
 for(unsigned n=0;n<1000000;n++){
 unsigned bytes=next()%16,base=next()%1501,flush=next()%8==0;
 int count=reference_count-(int)bytes;unsigned credit=base,stall=0;
 while(count<0){count++;if(credit>1)credit--;else stall++;}
 if(flush)count=0;else while(credit>=1&&count<6){credit--;count++;}
 unsigned result=world_cycles_reference_instruction(&own,bytes,base,flush);
 if(result!=base+stall || own.available_bytes!=(unsigned)count){printf("mismatch %u\n",n);return 1;}
 reference_count=count;
 }
 printf("1000000 reference instruction timing cases match, stalls=%llu\n",(unsigned long long)own.fetch_stall_cycles);
 return 0;
}
