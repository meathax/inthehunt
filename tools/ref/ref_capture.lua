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
--   patches.bin    with patch={addr,...}: for every call of a listed handler, the
--                  256-byte blocks of work/video RAM that differ after it returned
--                  (pass u32, call u32, count u16, then region u8 + offset u16 + block).
--                  check_replay replays these for handlers that are not recovered yet,
--                  so the rest of the pass can still be compared.
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
local regs_log, regs_hist = nil, {}
local patch_set, patch_file, patch_before, patch_call = nil, nil, nil, 0
if cfg.patch then
    patch_set = {}
    for _, a in ipairs(cfg.patch) do patch_set[a] = true end
    patch_file = open("patches.bin", "wb")
end
local watch_set, watch_file, last_target = nil, nil, 0
if cfg.watch or cfg.watch_master or cfg.watch_idle then
    watch_set = {}
    for _, a in ipairs(cfg.watch or {}) do watch_set[a] = true end
    watch_file = open("regs.txt")
end
local function patch_state()
    return { memory:read_range(0xe0000, 0xebfff, 8), memory:read_range(0xd0000, 0xdffff, 8) }
end
local function write_patch()
    local now, blocks = patch_state(), {}
    for region = 1, 2 do
        local a, b = patch_before[region], now[region]
        for offset = 1, #a, 256 do
            local chunk = b:sub(offset, offset + 255)
            if a:sub(offset, offset + 255) ~= chunk then
                blocks[#blocks + 1] = string.pack("<I1I2", region - 1, offset - 1) .. chunk
            end
        end
    end
    patch_file:write(string.pack("<I4I4I2", pass, patch_call, #blocks), table.concat(blocks))
    patch_before = nil
end
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
            elseif current and (current.master == 0xa0e or current.master == 0x24e2 or current.master == 0xa09 or current.master == 0x344d or current.master == 0x356e or current.master == 0x333d) then
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
    if cfg.refill and (not cfg.refill_until or frame < cfg.refill_until) and (memory:read_u8(0xe0264) & 1) ~= 0 and memory:read_u8(0xe0118) < 2 then poke(0x118, 3, 1) end
    if cfg.refill2 and (memory:read_u8(0xe0264) & 2) ~= 0 and memory:read_u8(0xe0158) < 2 then poke(0x158, 3, 1) end
    -- duel_keep=true: keep both submarines alive through the two-player duel so that it ends in a draw.
    if cfg.duel_keep and word(0x80) == 0x33a9 then
        for _, slot in ipairs({0x90, 0x92}) do
            local sub = word(slot)
            if memory:read_u8(0xe0000 + sub + 0x26) < 4 and memory:read_u8(0xe0000 + sub + 0x27) == 0 then poke(sub + 0x26, 8, 1) end
        end
    end
    if cfg.arm and frame >= (cfg.arm.from or 1500) and word(0x80) == 0xb2e and (memory:read_u8(0xe0264) & 1) ~= 0 then
        -- arm={mode=M, level=L, kind2=K, level2=L, from=FRAME}: keep P1 equipped with
        -- primary weapon mode M (2, 4 or 6) and/or secondary kind K (2 or 4), as the
        -- pickups would, so weapon paths can be recorded in any stage.
        local config = word(word(0x90) + 0x70)
        for _, f in ipairs({{"mode", 0x10}, {"level", 0x12}, {"kind2", 0x14}, {"level2", 0x16}}) do
            local want = cfg.arm[f[1]]
            if want and config ~= 0 and word(config + f[2]) ~= want then poke(config + f[2], want, 2) end
        end
    end
    if cfg.stage and not cfg.stage_done and word(0x80) == 0x2218 then poke(0x84, cfg.stage, 2); cfg.stage_done = true end
    -- pokes={{frame=F, addr=A, value=V, size=1|2, master=M}, ...}: one recorded poke each, at the
    -- first pass start at or after frame F (and, with master=M, while that master runs).
    for _, p in ipairs(cfg.pokes or {}) do
        if not p.done and frame >= (p.frame or 0) and (not p.master or word(0x80) == p.master) then
            poke(p.addr, p.value, p.size or 2); p.done = true
        end
    end
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
-- trace_pass=P[, trace_count=M] traces one range; trace={{P,M},{P,M},...} several.
local function traced(p)
    if cfg.trace_pass and p >= cfg.trace_pass and p < cfg.trace_pass + (cfg.trace_count or 1) then return true end
    for _, range in ipairs(cfg.trace or {}) do
        if p >= range[1] and p < range[1] + (range[2] or 1) then return true end
    end
    return false
end
local returns = {[0x897]=true, [0x8ae]=true, [0x8cc]=true, [0x907]=true, [0x925]=true}
tap_stack = memory:install_write_tap(0xefffc, 0xefffd, "ref_stack", function(addr, data, mask)
    if finished or saving or addr ~= 0xefffc or not returns[data & 0xffff] then return end
    if data == 0x897 and armed then armed = false; on_pass_start() end
    if not recording or not in_pass then return end
    if traced(pass) then
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
    if watch_set and (watch_set[target] or word(0x80) == cfg.watch_master or (cfg.watch_idle and (word(0x264) & 3) == 0 and word(0x80) == 0xb2e)) then
        -- watch={addr,...}: registers a listed handler inherits from the code before it.
        local r = cpu.state
        watch_file:write(("%d %d %05x prev %05x bp %04x aw %04x bw %04x cw %04x dw %04x ix %04x iy %04x ds1 %04x clock %04x active %04x v3a %04x v42 %04x\n"):format(
            pass, calls + 1, target, last_target, actor, r.AW.value, r.BW.value, r.CW.value, r.DW.value, r.IX.value, r.IY.value, r.DS1.value, word(0x274), word(0x264), word(actor + 0x3a), word(actor + 0x42)))
    end
    last_target = target
    if patch_set then
        saving = true
        if patch_before then write_patch() end
        if patch_set[target] then patch_before, patch_call = patch_state(), calls end
        saving = false
    end
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
    if patch_file then patch_file:close() end
    if watch_file then watch_file:close() end
    print(("ref_capture=%s passes=%d frame=%d"):format(reason, pass, frame))
    machine:exit()
end
-- Bot: coin, start, hold right, weave vertically, fire both weapons.
local function set(field, on) if on then field:set_value(1) else field:clear_value() end end
-- aim={[handler]=true or {dx=,dy=,face=,pri=,slack=}, ...}: while an actor with one of
-- these handler entries (linear address) exists, P1 holds station beside it: dx pixels
-- away (default -70, to its left) at its height plus dy (actor Y grows upward), facing
-- right (or face="left"), so torpedoes hit it. Among several, the lowest pri wins
-- (default 1), then list order. Returns the pad state or nil.
local rng, rnd = (cfg.random or 1) * 2654435761 % 0x7fffffff, 0
local function aim_at()
    if not cfg.aim then return nil end
    local a, n, best, opt = 0x400, 0, nil, nil
    while n < 300 do
        local target = ((word(a + 2) << 4) + word(a)) & 0xfffff
        if target == 0x96a then break end
        local how = cfg.aim[target]
        if how then
            local o = type(how) == "table" and how or {}
            if not best or (o.pri or 1) < (opt.pri or 1) then best, opt = a, o end
        end
        a = word(a + 4); n = n + 1
    end
    if not best then return nil end
    local sub = word(0x90)
    local x, y = word(sub + 0x21), word(sub + 0x24)
    local ex = ((word(best + 0x21) + (opt.dx or -70) - x + 0x8000) & 0xffff) - 0x8000
    local ey = ((word(best + 0x24) + (opt.dy or 0) - y + 0x8000) & 0xffff) - 0x8000
    local slack = opt.slack or 20
    local facing_left = (memory:read_u8(0xe0000 + sub + 0x29) & 1) ~= 0
    local inside = ex >= -slack and ex <= slack
    local right, left = ex > slack, ex < -slack
    if inside and opt.face == "left" then left = not facing_left
    elseif inside then right = facing_left end
    return {right=right, left=left, up=ey > 3, down=ey < -3}
end
frame_sub = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    if finished then return end
    if cfg.frames and frame >= cfg.frames then finish("frames"); return end
    if cfg.verbose and frame % 500 == 0 then print(("frame %d pass %d master %04x stage %d lives %d camera %04x sub %d,%d"):format(frame, pass, word(0x80), word(0x84), memory:read_u8(0xe0118), word(0xb700), word(word(0x90) + 0x21), word(word(0x90) + 0x24))) end
    if cfg.stop_stage and word(0x84) >= cfg.stop_stage and word(0x80) == 0xb2e then finish("stage"); return end
    if cfg.bot ~= false then
        set(coin["Coin 1"], frame >= 1200 and frame < 1206)
        set(pad["P1 Start"] or coin["1 Player Start"], frame >= 1280 and frame < 1286)
        local play = frame >= 1400
        local phase = (frame // 90) % 4
        local right, left, up, down = phase ~= 3, false, phase == 1, phase == 3
        -- surface=true keeps the submarine near the surface, where its second weapon fires upward.
        if cfg.surface then up, down = phase ~= 3, false end
        if cfg.track_kind and play then
            -- Boss helper: stay under the actor of this kind (word +0E) and keep surfacing.
            local player = word(0x90)
            for actor = 0x500, 0x8380, 0x80 do
                if word(actor + 0x0e) == cfg.track_kind and word(actor) ~= 0xffff and word(actor + 0x3c) ~= 0xffff then
                    local dx = ((word(actor + 0x21) + (cfg.track_dx or 0) - word(player + 0x21) + 0x8000) & 0xffff) - 0x8000
                    right, left = dx > 6, dx < -6
                    up, down = (frame // 40) % 4 ~= 3, (frame // 40) % 4 == 3 and cfg.track_dive == true
                    break
                end
            end
        end
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
                right, left = true, false
                down = t < sink
                up = t >= sink and t < sink + attempt * rise
            end
        end
        -- steer_from=FRAME, steer_x=X: from that frame on keep P1 near X
        -- instead of pushing right (a boss that must be fought from the left).
        if play and cfg.steer_from and frame >= cfg.steer_from then
            local x = word(word(0x90) + 0x21)
            right, left = x < cfg.steer_x - 8, x > cfg.steer_x + 8
        end
        -- follow=true: steer through a vertical scroll section. RAM[270] is 2
        -- while the camera waits for a player low on the screen (actor Y grows
        -- upwards) and 4 while it waits for one high on the screen.
        if cfg.follow and word(0x80) == 0xb2e then
            local vertical = word(0x270)
            up = vertical == 4 or (up and vertical ~= 2)
            down = vertical == 2 or (down and vertical ~= 4)
        end
        if cfg.climb then
            -- climb=true: upward stages. Stay near the top of the view and
            -- sweep sideways every climb_period frames.
            local sweep = (frame // (cfg.climb_period or 150)) % 2
            right, left, up, down = sweep == 0, sweep == 1, true, false
        end
        local aim = play and aim_at()
        if aim then right, left, up, down = aim.right, aim.left, aim.up, aim.down end
        local fire1, fire2 = frame % 12 < 6, frame % 16 < 8
        if cfg.random and frame >= (cfg.random_from or 0) and frame < (cfg.random_until or math.huge) then
            -- random=SEED: a different deterministic player per seed. Every 24
            -- frames pick a new direction (biased to the right so the stage
            -- advances) and new fire habits, to reach paths the weaving bot misses.
            if frame % 24 == 0 then
                rng = (rng * 1103515245 + 12345) & 0x7fffffff
                rnd = rng >> 8
            end
            right, left = rnd % 8 < 5, rnd % 8 == 5
            up, down = (rnd >> 3) % 4 == 0, (rnd >> 3) % 4 == 1
            fire1 = (rnd >> 5) % 4 ~= 0 and frame % 12 < 6
            fire2 = (rnd >> 7) % 4 ~= 0 and frame % 16 < 8
        end
        set(pad["P1 Right"], play and right)
        set(pad["P1 Left"], play and left)
        set(pad["P1 Up"], play and up)
        set(pad["P1 Down"], play and down)
        set(pad["P1 Button 1"], play and fire1)
        set(pad["P1 Button 2"], play and fire2)
        if cfg.p2 then
            -- p2=true: a second credit and player two, who mirrors player one
            -- vertically. p2_join=FRAME joins mid-game instead of at the start.
            local join = cfg.p2_join or 1300
            set(coin["Coin 2"], frame >= 1220 and frame < 1226)
            set(coin["2 Players Start"], frame >= join and frame < join + 6)
            local on = frame >= join + 100
            set(pad["P2 Right"], on and right)
            set(pad["P2 Left"], on and left)
            set(pad["P2 Up"], on and down)
            set(pad["P2 Down"], on and up)
            set(pad["P2 Button 1"], on and frame % 14 < 7)
            set(pad["P2 Button 2"], on and frame % 18 < 9)
        end
        -- coin_at={F,...}: extra Coin 1 presses; start_at={F,...}: extra P1 Start presses (continue).
        for _, f in ipairs(cfg.coin_at or {}) do if frame >= f and frame < f + 6 then set(coin["Coin 1"], true) end end
        for _, f in ipairs(cfg.start_at or {}) do if frame >= f and frame < f + 6 then set(pad["P1 Start"] or coin["1 Player Start"], true) end end
        if cfg.continue and play and (frame % 600) < 6 and (memory:read_u8(0xe0264) & 1) == 0 then
            set(coin["Coin 1"], true)
        end
        if cfg.continue and play and (frame % 600) >= 60 and (frame % 600) < 66 and (memory:read_u8(0xe0264) & 1) == 0 then
            set(pad["P1 Start"] or coin["1 Player Start"], true)
        end
    end
end)
