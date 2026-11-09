-- Exercise normal cabinet inputs after the World boot sequence finishes.
local machine = manager.machine
local coin = machine.ioport.ports[":COINS_DSW3"].fields
local pad = machine.ioport.ports[":P1_P2"].fields
local frame = 0
if capture_encounter then capture_encounter(machine, function() return frame end) end
local function pulse(field, first, last)
    if frame == first then field:set_value(1) end
    if frame == last then field:clear_value() end
end
local ram=machine.devices[":maincpu"].spaces["program"]
local io_library = io
local io=machine.devices[":maincpu"].spaces["io"]
-- Both guests run through the M92 interpreter, including rebuilt 16-bit C.
-- Sample each processor independently; counts are frames, not instructions.
local fallback = {maincpu={}, soundcpu={}}
local function sample_fallback()
    for name, counts in pairs(fallback) do
        local pc = machine.devices[":"..name].state.CURPC.value
        counts[pc] = (counts[pc] or 0) + 1
    end
end
local sound_commands=0
local fm_writes, rebuilt_inputs = 0, 0
local fm_hash = 0
local sound_cpu = machine.devices[":soundcpu"]
fm_tap = sound_cpu.spaces.program:install_write_tap(0xa8040, 0xa8043, "world_fm", function(address, data)
    if frame < 2500 then return end
    fm_writes = fm_writes + 1
    fm_hash = ((fm_hash * 33) ~ (address & 3) ~ (data & 0xff)) & 0xffffffff
end)
input_tap = io:install_read_tap(6, 7, "world_inputs", function()
    local pc = machine.devices[":maincpu"].state.CURPC.value
    if pc >= 0xff24 and pc < 0xff64 then rebuilt_inputs = rebuilt_inputs + 1 end
end)
sound_tap=io:install_write_tap(0,1,"world_sound",function(addr,data,mask)
    if frame>=1300 then sound_commands=sound_commands+1 end
end)
frame_subscription = emu.add_machine_frame_notifier(function()
    frame=frame+1
    if world_capture_end and frame > world_capture_end then machine:exit(); return end
    if frame >= 2500 and frame < 3050 then sample_fallback() end
    pulse(coin["Coin 1"], 1200, 1206)
    pulse(coin["1 Player Start"], 1280, 1286)
    pulse(pad["P1 Right"], 2550, 2625)
    pulse(pad["P1 Up"], 2625, 2660)
    if world_capture_end then pulse(pad["P1 Right"], 3700, 6001) end
    if (frame >= 2500 and frame < 3050) or (world_capture_end and frame >= 3700) then
        pad["P1 Button 1"]:set_value(frame % 12 < 6 and 1 or 0)
        pad["P1 Button 2"]:set_value(frame % 16 < 8 and 1 or 0)
    end
    if frame == 3050 then
        pad["P1 Button 1"]:clear_value()
        pad["P1 Button 2"]:clear_value()
        machine.video:snapshot()
        print(("stage=%d lives=%d sound_commands=%d"):format(ram:read_u8(0xe0084),ram:read_u8(0xe0118),sound_commands))
        print(("fm_writes=%d fm_hash=%08x rebuilt_inputs=%d"):format(fm_writes,fm_hash,rebuilt_inputs))
        for name, counts in pairs(fallback) do
            local total = 0
            local report = assert(io_library.open("../world/fallback-" .. name .. ".tsv","w"))
            report:write("guest_pc\tsampled_frames\n")
            local addresses = {}
            for pc in pairs(counts) do table.insert(addresses,pc) end
            table.sort(addresses)
            for _, pc in ipairs(addresses) do
                report:write(("%05x\t%d\n"):format(pc, counts[pc]))
                total = total + counts[pc]
            end
            report:close()
            print(("fallback cpu=%s guest_frames=%d"):format(name,total))
        end
        if not world_capture_end or world_capture_end <= frame then machine:exit() end
    end
end)
