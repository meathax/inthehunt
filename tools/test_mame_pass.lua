-- Cases are a Lua array of {name=..., path=...}; snapshots and ROMs stay private.
local cases=dofile(assert(os.getenv("REFERENCE_CASES")))

local cpu=manager.machine.devices[":maincpu"]
local mem=cpu.spaces.program
local base=os.getenv("REFERENCE_OUT")
local frame,index,started=0,0,0
local recording,saving=false,false
local function save(name,lo,hi)
 local f=assert(io.open(base.."/"..cases[index].name.."."..name,"wb"))
 f:write(mem:read_range(lo,hi,8));f:close()
end
tap=mem:install_read_tap(0xe0400,0xe8481,"native_pass_compare",function(addr)
 if recording and not saving and addr==0xe0480 and cpu.state.CURPC.value==0x922 then
  saving=true;save("work",0xe0000,0xeffff);save("video",0xd0000,0xdffff);saving=false
  recording=false
  print("PASS_CAPTURE "..cases[index].name)
 end
end)
local function inject(c)
 local f=assert(io.open(c.path,"rb"));local data=f:read("a");f:close()
 local function w(n)return data:byte(n+1)|(data:byte(n+2)<<8)end
 saving=true
 for n=0,65534,2 do mem:write_u16(0xe0000+n,w(n));mem:write_u16(0xd0000+n,w(65536+n)) end
 saving=false
 -- Execute the original dispatcher without asynchronous IRQs, exactly like the single-pass native harness.
 cpu.state.PSW.value=0x8002;cpu.state.PS.value=0;cpu.state.PC.value=0x886
 cpu.state.SS.value=0xe000;cpu.state.DS0.value=0xe000;cpu.state.DS1.value=0xd000;cpu.state.SP.value=0xff00
 for _,reg in ipairs({"AW","BW","CW","DW","IX","IY","BP"}) do cpu.state[reg].value=0 end
 recording=true;started=frame
end
subscription=emu.add_machine_frame_notifier(function()
 frame=frame+1
 if frame<60 then return end
 if recording then
  if frame-started>60 then print("PASS_TIMEOUT "..cases[index].name.." "..string.format("%x",cpu.state.CURPC.value));recording=false end
  return
 end
 index=index+1
 if index>#cases then manager.machine:exit();return end
 inject(cases[index])
end)
