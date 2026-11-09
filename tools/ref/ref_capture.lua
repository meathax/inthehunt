-- Reference recorder for the World set running in MAME (dev tool, never shipped).
-- Configure with a prepended table:  REF = { out="dir", stage=N|nil, frames=N, ... }
-- Output (all private, under .local):
--   initial-{work,video,oam,palette}  state at the first recorded actor pass
--   hardware.txt   WH1 board events between passes (B/I/R/P) plus W pokes
--   irqs.txt       busy VBlanks inside passes: pass call system
--   passes.bin     per pass: 16-byte header (pass, frame, irq tick, master, stage)
--                  + work RAM 64K + video RAM 64K + OAM 2K + palette 2K
--   trace.bin      with trace_pass=N[, trace_count=M]: state before every handler
--                  call of those passes (8-byte header, work 48K, video 64K)
--   stages.txt     stage, first pass, frame for each stage reached
--   calls.txt      per pass: handler address sequence (linear) for the coverage ledger
--   sound.txt      outgoing sound commands per pass
local cfg = REF or {}
local out = cfg.out or "ref"
local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local memory = cpu.spaces.program
local ports = machine.ioport.ports
local coin = ports[":COINS_DSW3"].fields
local pad = ports[":P1_P2"].fields
local frame, pass = 0, 0
local bot_camera, bot_still = nil, 0
local recording, finished, saving = false, false, false
local function open(name, mode) return assert(io.open(out .. "/" .. name, mode or "w")) end
local hardware, irq_log, sounds, calls_log = open("hardware.txt"), open("irqs.txt"), open("sound.txt"), open("calls.txt")
local passes = open("passes.bin", "wb")
local stage_log = open("stages.txt")
hardware:write("WH1\n")
local polls, system_port, last_irq, calls, in_pass = 0, 0xffff, 0, 0, false
local pending, current, seen_ports, handlers = {}, nil, {}, {}
local stages_seen = {}
local trace = nil
local regions = { {"work", 0xe0000, 0xeffff}, {"video", 0xd0000, 0xdffff},
                  {"oam", 0xf8000, 0xf87ff}, {"palette", 0xf8800, 0xf8fff} }
local function dump(label)
    saving = true
    for _, r in ipairs(regions) do
        local f = open(label .. "-" .. r[1], "wb")
        f:write(memory:read_range(r[2], r[3], 8)); f:close()
    end
    saving = false
end
local function word(a) return memory:read_u16(0xe0000 + a) end
local function poke(addr, value, size)
    -- Applied at the pass-start boundary in both MAME and the native replay.
    if size == 1 then memory:write_u8(0xe0000 + addr, value) else memory:write_u16(0xe0000 + addr, value) end
    table.insert(pending, ("W %04x %0" .. (size == 1 and 2 or 4) .. "x"):format(addr, value))
end
local function emit_interval()
    for _, e in ipairs(pending) do
        if type(e) == "table" then
            hardware:write(("B %04x %04x %04x %04x %u %d\n"):format(e[0], e[6], e[2], e[4], e.polls, e.busy or -1))
        else hardware:write(e, "\n") end
    end
    hardware:write("P\n")
    pending, current = {}, nil
end
-- Idle-spin count: RAM[B892] at the master call minus its value at the 0946
-- DMA wait that precedes the spin (only the 084D spin changes it in between).
local b892_at_end = nil
tap_master = memory:install_read_tap(0xe0080, 0xe0081, "ref_master", function(_, data)
    if recording and not saving and cpu.state.CURPC.value == 0x878 then
        local spins = memory:read_u16(0xeb892)
        polls = b892_at_end and ((spins - b892_at_end) & 0xffff) or 0
        current = {[0]=ports[":P1_P2"]:read() & 0xffff, [2]=system_port, [4]=ports[":DSW"]:read() & 0xffff,
                   [6]=ports[":P3_P4"]:read() & 0xffff, polls=polls, master=data & 0xffff}
        table.insert(pending, current)
    end
end)
tap_sound = cpu.spaces.io:install_write_tap(0, 1, "ref_sound", function(_, data)
    if recording then sounds:write(("%d %04x\n"):format(pass, data & 0xff)) end
end)
tap_io = cpu.spaces.io:install_read_tap(0, 9, "ref_ports", function(addr, data)
    if not recording then return end
    local pc = cpu.state.CURPC.value
    if pc == 0x946 and addr == 2 then
        -- DMA wait before every idle spin, with or without an actor pass.
        b892_at_end = memory:read_u16(0xeb892)
        return
    end
    if pc == 0x4ec and addr == 8 then
        table.insert(pending, ("R %04x"):format(data & 0xffff))
    elseif pc == 0x69b and addr == 2 then
        if (memory:read_u16(0xe025e) & 0x8000) ~= 0 then
            if in_pass then
                irq_log:write(("%d %d %04x\n"):format(pass, calls, data & 0xffff))
            elseif current and (current.master == 0xa0e or current.master == 0x24e2) then
                current.busy = data & 0xffff
            else
                table.insert(pending, ("I %04x"):format(data & 0xffff))
            end
        else
            system_port = data & 0xffff
        end
    elseif pc == 0x5e21 or pc == 0x5e7a then
        -- The VBlank IRQ's own pad latch belongs to the next master, which samples
        -- the ports itself. Letting it update the previous record corrupted the pad
        -- word of masters that run no actor pass (stage transitions).
    elseif current and (addr == 0 or addr == 4 or addr == 6) then
        current[addr] = data & 0xffff
    end
end)
-- The pass counter at RAM[274] advances once per board boundary; only the first
-- dispatcher read after it is a pass start (MAME re-reads e0400 at 0894 otherwise).
local armed = false
tap_counter = memory:install_write_tap(0xe0274, 0xe0275, "ref_counter", function() armed = true end)
local dispatch_pc = {[0x894]=true, [0x8ab]=true, [0x8c9]=true, [0x904]=true, [0x922]=true}
local start_frame = cfg.start or 600
local function on_pass_start()
    -- Runs at the dispatcher's first far call: the native "after dispatch_begin" point.
    -- Scripted state edits happen here, recorded as W events before this pass's P,
    -- so the native replay applies them after the boundary and before the pass.
    -- They also run before the recording window opens, so a late window sees
    -- the same game as a recording started earlier.
    if cfg.refill and (memory:read_u8(0xe0264) & 1) ~= 0 and memory:read_u8(0xe0118) < 2 then poke(0x118, 3, 1) end
    if cfg.stage and not cfg.stage_done and word(0x80) == 0x2218 then poke(0x84, cfg.stage, 2); cfg.stage_done = true end
    if not recording and frame < start_frame then pending = {}; return end
    if not recording then
        recording = true
        pending = {}
        dump("initial")
    else
        emit_interval()
    end
    pass = pass + 1
    calls, in_pass = 0, true
    local irq = word(0x276)
    last_irq = irq
    local stage = word(0x84)
    if word(0x80) == 0xb2e and not stages_seen[stage] then
        stages_seen[stage] = true
        print(("stage %d first pass %d frame %d"):format(stage, pass, frame))
        stage_log:write(("%d %d %d\n"):format(stage, pass, frame)); stage_log:flush()
    end
    if cfg.snap_every and pass % cfg.snap_every == 0 then machine.video:snapshot() end
    calls_log:write(pass, " ", frame, " ", ("%04x"):format(word(0x80)), " ", stage)
    if cfg.passes == false then return end
    saving = true
    passes:write(string.pack("<I4I4I2I2I2I2", pass, frame, irq, word(0x80), stage, 0))
    passes:write(memory:read_range(0xe0000, 0xeffff, 8))
    passes:write(memory:read_range(0xd0000, 0xdffff, 8))
    passes:write(memory:read_range(0xf8000, 0xf8fff, 8))
    saving = false
end
-- Dispatcher calls run with SS:SP = E000:0000, so each far call pushes its
-- return offset at E000:FFFC. Watching that word is far cheaper than a read
-- tap over the actor area. The push happens before the handler runs.
local returns = {[0x897]=true, [0x8ae]=true, [0x8cc]=true, [0x907]=true, [0x925]=true}
tap_stack = memory:install_write_tap(0xefffc, 0xefffd, "ref_stack", function(addr, data, mask)
    if finished or saving or addr ~= 0xefffc or not returns[data & 0xffff] then return end
    if data == 0x897 and armed then armed = false; on_pass_start() end
    if not recording or not in_pass then return end
    if cfg.trace_pass and pass >= cfg.trace_pass and pass < cfg.trace_pass + (cfg.trace_count or 1) then
        -- State before each handler call (and before the terminal actor).
        saving = true
        trace = trace or open("trace.bin", "wb")
        trace:write(string.pack("<I4I4", pass, calls))
        trace:write(memory:read_range(0xe0000, 0xebfff, 8))
        trace:write(memory:read_range(0xd0000, 0xdffff, 8))
        saving = false
    end
    local actor = cpu.state.BP.value
    local target = ((word(actor + 2) << 4) + word(actor)) & 0xfffff
    if target == 0x96a then
        in_pass = false
        calls_log:write("\n")
    else
        calls = calls + 1
        calls_log:write((" %05x"):format(target))
    end
end)
local function finish(reason)
    if finished then return end
    finished = true
    emit_interval()
    for _, f in ipairs({hardware, irq_log, sounds, calls_log, passes, stage_log}) do f:close() end
    if trace then trace:close() end
    print(("ref_capture=%s passes=%d frame=%d"):format(reason, pass, frame))
    machine:exit()
end
-- Bot: coin, start, hold right, weave vertically, fire both weapons.
local function set(field, on) if on then field:set_value(1) else field:clear_value() end end
frame_sub = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    if finished then return end
    if cfg.frames and frame >= cfg.frames then finish("frames"); return end
    if cfg.verbose and frame % 500 == 0 then print(("frame %d pass %d master %04x stage %d lives %d camera %04x"):format(frame, pass, word(0x80), word(0x84), memory:read_u8(0xe0118), word(0xb700))) end
    if cfg.stop_stage and word(0x84) >= cfg.stop_stage and word(0x80) == 0xb2e then finish("stage"); return end
    if cfg.bot ~= false then
        set(coin["Coin 1"], frame >= 1200 and frame < 1206)
        set(pad["P1 Start"] or coin["1 Player Start"], frame >= 1280 and frame < 1286)
        local play = frame >= 1400
        local phase = (frame // 90) % 4
        set(pad["P1 Right"], play and phase ~= 3)
        set(pad["P1 Left"], false)
        set(pad["P1 Up"], play and phase == 1)
        set(pad["P1 Down"], play and phase == 3)
        if cfg.unstick then
            -- unstick=N: when the camera stops advancing, stop weaving. Sink to the
            -- seabed, rise for a while (longer on each of N attempts), then hold
            -- that depth so every shot lands on the same obstacle.
            local camera = word(0xb700)
            if camera ~= bot_camera then bot_camera, bot_still = camera, 0 else bot_still = bot_still + 1 end
            local stuck = bot_still - (cfg.unstick_after or 120)
            if play and stuck > 0 then
                local period, sink, rise = cfg.unstick_period or 420, cfg.unstick_sink or 110, cfg.unstick_rise or 22
                local t, attempt = stuck % period, (stuck // period) % cfg.unstick
                set(pad["P1 Right"], true)
                set(pad["P1 Down"], t < sink)
                set(pad["P1 Up"], t >= sink and t < sink + attempt * rise)
            end
        end
        set(pad["P1 Button 1"], play and frame % 12 < 6)
        set(pad["P1 Button 2"], play and frame % 16 < 8)
        if cfg.continue and play and (frame % 600) < 6 and (memory:read_u8(0xe0264) & 1) == 0 then
            set(coin["Coin 1"], true)
        end
    end
end)
