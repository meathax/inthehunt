-- Capture the two endpoints of the native first-encounter replay and its raw inputs.
-- Called from stage_one.lua using the reference World board.
world_capture_end = 6000
function capture_encounter(machine, get_frame)
    local cpu = machine.devices[":maincpu"]
    local memory = cpu.spaces.program
    local path = "../world/encounter-"
    local inputs = assert(io.open(path .. "inputs.txt", "w"))
    local recording, finished, saving = false, false, false
    local sounds = assert(io.open(path .. "sound.txt", "w"))
    local pass, last_irq = 0, 0
    local frames = assert(io.open(path .. "frames.txt", "w"))
    encounter_sound = cpu.spaces.io:install_write_tap(0, 1, "encounter_sound", function(addr, data)
        if recording then
            assert(sounds:write(("transition %d sound %04x\n"):format(pass, data & 0xff)))
        end
    end)
    local hardware = assert(io.open(path .. "hardware.txt", "w"))
    hardware:write("WH1\n")
    local polls, system_port = 0, 0xffff
    local pending, current = {}, nil
    local function emit_interval()
        for _, event in ipairs(pending) do
            if type(event) == "table" then
                hardware:write(("B %04x %04x %04x %04x %u %d\n"):format(
                    event[0], event[6], event[2], event[4], event.polls, event.busy or -1))
            else
                hardware:write(event, "\n")
            end
        end
        hardware:write("P\n")
        pending, current = {}, nil
    end
    encounter_polls = memory:install_write_tap(0xeb892, 0xeb893, "encounter_polls", function()
        if recording and cpu.state.CURPC.value == 0x84d then polls = polls + 1 end
    end)
    encounter_master = memory:install_read_tap(0xe0080, 0xe0081, "encounter_master", function(_, data)
        if recording and not saving and not world_capture_saving and cpu.state.CURPC.value == 0x878 then
            current = {[0]=machine.ioport.ports[":P1_P2"]:read() & 0xffff,
                       [2]=system_port, [4]=machine.ioport.ports[":DSW"]:read() & 0xffff,
                       [6]=machine.ioport.ports[":P3_P4"]:read() & 0xffff, polls=polls, master=data & 0xffff}
            polls = 0
            table.insert(pending, current)
        end
    end)
    local ports = {}
    local irq_log = assert(io.open(path .. "irqs.txt", "w"))
    local calls = 0
    local in_pass = false
    local dispatch_pc = {[0x894]=true, [0x8ab]=true, [0x8c9]=true, [0x904]=true, [0x922]=true}
    local function save(label)
        saving = true
        world_capture_saving = true
        for _, region in ipairs({
            {"work", 0xe0000, 0xeffff}, {"video", 0xd0000, 0xdffff},
            {"oam", 0xf8000, 0xf87ff}, {"palette", 0xf8800, 0xf8fff}
        }) do
            local file = assert(io.open(path .. label .. "-" .. region[1], "wb"))
            assert(file:write(memory:read_range(region[2], region[3], 8)))
            assert(file:close())
        end
        saving = false
        world_capture_saving = false
    end
    encounter_io = cpu.spaces.io:install_read_tap(0, 9, "encounter_ports", function(addr, data)
        if recording then
            ports[addr] = data & 0xffff
            local pc = cpu.state.CURPC.value
            if pc == 0x4ec and addr == 8 then
                table.insert(pending, ("R %04x"):format(data & 0xffff))
            elseif pc == 0x69b and addr == 2 then
                if (memory:read_u16(0xe025e) & 0x8000) ~= 0 then
                    if not in_pass then
                        if current and (current.master == 0xa0e || current.master == 0x24e2) then
                            current.busy = data & 0xffff
                        else
                            table.insert(pending, ("I %04x"):format(data & 0xffff))
                        end
                    end
                else
                    system_port = data & 0xffff
                end
            elseif current and (addr == 0 or addr == 4 or addr == 6) then
                current[addr] = data & 0xffff
            end
            if in_pass and addr == 2 and cpu.state.CURPC.value == 0x69b and
                    (memory:read_u16(0xe025e) & 0x8000) ~= 0 then
                last_irq = (last_irq + 1) & 0xffff
                assert(irq_log:write(("%d %d %04x\n"):format(pass, calls, data & 0xffff)))
            end
        end
    end)
    encounter_pass = memory:install_read_tap(0xe0400, 0xe8481, "encounter_pass", function(addr)
        if finished or saving or world_capture_saving then return end
        local frame, pc = get_frame(), cpu.state.CURPC.value
        if addr == 0xe0400 and pc == 0x894 then
            if frame == 580 or recording then
                calls, in_pass = 0, true
                pass = pass + 1
                assert(frames:write(frame, "\n"))
            end
            local irq = memory:read_u16(0xe0276)
            if frame == 580 then
                save("initial")
                recording = true
            elseif recording then
                emit_interval()
                local function port(offset, name)
                    return ports[offset] or (machine.ioport.ports[name]:read() & 0xffff)
                end
                assert(inputs:write(("%04x %04x %04x %04x %u\n"):format(
                    port(0, ":P1_P2"), port(6, ":P3_P4"),
                    port(2, ":COINS_DSW3"), port(4, ":DSW"), (irq - last_irq) & 0xffff)))
                ports = {}
            end
            last_irq = irq
        end
        if recording and dispatch_pc[pc] and addr == 0xe0000 + cpu.state.BP.value then
            if addr ~= 0xe0480 then calls = calls + 1 else in_pass = false end
        end
        if recording and frame == world_capture_end and addr == 0xe0480 and pc == 0x922 then
            save("final")
            assert(inputs:close())
            assert(sounds:close())
            assert(frames:close())
            assert(irq_log:close())
            assert(hardware:close())
            recording, finished = false, true
            print("encounter_capture=complete")
        end
    end)
end
