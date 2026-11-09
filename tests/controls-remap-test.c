#include "../src/world_input.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){printf("FAIL line%d %s\n",__LINE__,#c);return 1;}}while(0)
static int ports(WorldInput *s,unsigned pads,unsigned system){uint16_t p,b;world_input_ports(s,&p,&b);return p==pads&&b==system;}
int main(void){
 WorldInput in;world_input_init(&in,0);CHECK(world_input_bindings_valid(&in.bindings));CHECK(ports(&in,0xffff,0xffff));
 world_input_key(&in,0x27,1);world_input_key(&in,'X',1);world_input_key(&in,'5',1);CHECK(ports(&in,0xffbe,0xfffb));
 world_input_key(&in,'A',1);world_input_key(&in,'G',1);world_input_key(&in,'2',1);world_input_key(&in,'6',1);CHECK(ports(&in,0x7dbe,0xfff1));
 world_input_set_focus(&in,0);CHECK(ports(&in,0xffff,0xffff));world_input_key(&in,'Z',1);world_input_set_focus(&in,1);CHECK(ports(&in,0xffff,0xffff));
 WorldInputController pad={0};pad.connected=1;pad.left_x=16000;pad.buttons=0x3020;world_input_controller(&in,0,&pad);CHECK(ports(&in,0xff3e,0xfffb));
 world_input_controller(&in,0,0);CHECK(ports(&in,0xffff,0xffff));
 world_input_key(&in,13,1);CHECK(world_input_capture_begin(&in,0,WORLD_INPUT_FIRE1,WORLD_INPUT_KEYBOARD));CHECK(world_input_capture_poll(&in,0)==WORLD_INPUT_CAPTURE_WAITING);
 world_input_key(&in,'Q',1);CHECK(world_input_capture_poll(&in,0)==WORLD_INPUT_CAPTURE_WAITING);
 world_input_key(&in,13,0);world_input_key(&in,'Q',0);CHECK(world_input_capture_poll(&in,0)==WORLD_INPUT_CAPTURE_WAITING);
 world_input_key(&in,'Q',1);uint16_t code=0;CHECK(world_input_capture_poll(&in,&code)==WORLD_INPUT_CAPTURE_BOUND);CHECK(code=='Q');CHECK(in.bindings.keyboard[0][WORLD_INPUT_FIRE1]=='Q');CHECK(ports(&in,0xffff,0xffff));
 CHECK(world_input_capture_poll(&in,0)==WORLD_INPUT_CAPTURE_NONE);world_input_key(&in,'Q',0);world_input_key(&in,'Q',1);CHECK(ports(&in,0xff7f,0xffff));world_input_key(&in,'Q',0);
 CHECK(world_input_capture_begin(&in,1,WORLD_INPUT_FIRE2,WORLD_INPUT_CONTROLLER));CHECK(world_input_capture_poll(&in,0)==WORLD_INPUT_CAPTURE_WAITING);
 memset(&pad,0,sizeof pad);pad.connected=1;pad.right_trigger=64;world_input_controller(&in,1,&pad);CHECK(world_input_capture_poll(&in,&code)==WORLD_INPUT_CAPTURE_BOUND);CHECK(code==WORLD_PAD_RIGHT_TRIGGER);CHECK(ports(&in,0xffff,0xffff));
 pad.right_trigger=0;world_input_controller(&in,1,&pad);pad.right_trigger=64;world_input_controller(&in,1,&pad);CHECK(ports(&in,0xbfff,0xffff));world_input_controller(&in,1,0);CHECK(ports(&in,0xffff,0xffff));
 CHECK(world_input_capture_begin(&in,0,WORLD_INPUT_COIN,WORLD_INPUT_CONTROLLER));world_input_key(&in,27,1);CHECK(world_input_capture_poll(&in,0)==WORLD_INPUT_CAPTURE_CANCELLED);CHECK(ports(&in,0xffff,0xffff));world_input_key(&in,27,0);
 CHECK(world_input_capture_begin(&in,0,WORLD_INPUT_START,WORLD_INPUT_KEYBOARD));world_input_set_focus(&in,0);CHECK(world_input_capture_poll(&in,0)==WORLD_INPUT_CAPTURE_CANCELLED);world_input_set_focus(&in,1);CHECK(ports(&in,0xffff,0xffff));
 WorldInputBindings invalid=in.bindings;invalid.controller[0][0]=11;CHECK(!world_input_apply_bindings(&in,&invalid));CHECK(world_input_bindings_valid(&in.bindings));
 invalid=in.bindings;invalid.keyboard[0][0]=256;CHECK(!world_input_bindings_valid(&invalid));invalid.keyboard[0][0]=27;CHECK(!world_input_bindings_valid(&invalid));
 char text[64];world_input_binding_name(WORLD_INPUT_CONTROLLER,WORLD_PAD_RIGHT_TRIGGER,text,sizeof text);CHECK(!strcmp(text,"Right trigger"));
 world_input_binding_name(WORLD_INPUT_KEYBOARD,0x27,text,sizeof text);CHECK(!strcmp(text,"Right arrow"));
 puts("default cabinet mappings, two players, focus loss, disconnect, keyboard/controller capture, cancellation and validation pass");return 0;
}
