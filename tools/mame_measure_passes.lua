-- Usage (MAME 0.289, World set; the PLD dumps are not emulated, so placeholder
-- files of the right size suffice):
--   MEASURE_FRAMES=80000 MEASURE_OUT=solo.tsv MEASURE_MODE=solo|duo
--   mame inthunt -autoboot_script tools/mame_measure_passes.lua -nothrottle -video none -sound none
-- Then: python tools/fit_slowdown.py solo.tsv duo.tsv
-- Busy time of every logic pass of the original World program, with the actor
-- routines that ran. A pass starts when the program bumps its pass counter
-- (RAM 0274) and ends at the first write of the 084D wait loop (RAM B892).
local frames = tonumber(os.getenv("MEASURE_FRAMES") or "40000")
local out = io.open(os.getenv("MEASURE_OUT") or "passes.tsv", "w")
local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local screen = manager.machine.screens[":screen"]
local ports = manager.machine.ioport.ports
local function field(port, name) return ports[port].fields[name] end
local inputs = {
  coin = field(":COINS_DSW3", "Coin 1"), start = field(":COINS_DSW3", "1 Player Start"),
  right = field(":P1_P2", "P1 Right"), left = field(":P1_P2", "P1 Left"),
  up = field(":P1_P2", "P1 Up"), down = field(":P1_P2", "P1 Down"),
  b1 = field(":P1_P2", "P1 Button 1"), b2 = field(":P1_P2", "P1 Button 2"),
}
local duo = os.getenv("MEASURE_MODE") == "duo"
p2 = {
  coin = field(":COINS_DSW3", "Coin 2"), start = field(":COINS_DSW3", "2 Players Start"),
  right = field(":P1_P2", "P2 Right"), left = field(":P1_P2", "P2 Left"),
  up = field(":P1_P2", "P2 Up"), down = field(":P1_P2", "P2 Down"),
  b1 = field(":P1_P2", "P2 Button 1"), b2 = field(":P1_P2", "P2 Button 2"),
}
local function r16(a) return space:read_u16(0xe0000 + a) end
local function now() return manager.machine.time:as_double() end

local idle_time, idle_frame = nil, nil
local function handlers()
  local list, actor, n = {}, 0x400, 0
  while n < 512 do
    local addr = ((r16(actor + 2) << 4) + r16(actor)) & 0xfffff
    if addr == 0x096a then break end
    list[#list + 1] = string.format("%05x", addr)
    actor = r16(actor + 4)
    n = n + 1
  end
  return table.concat(list, ";")
end
out:write("start\tcycles\tframes\tmaster\tstage\tsprites\thandlers\tvram\n")
-- A pass starts at the pass-counter increment at 0:086C (after the 084D
-- wait loop sees VBlank) and ends at the first write the 084D wait loop
-- makes afterwards; other code also writes B892, so the PC is checked.
local pc_state = cpu.state["PC"]
local pass_time, pass_frame = nil, nil
local vram_writes, copy_colours = 0, 0
fade = {[0] = 0, 0, 0}
taps = {
  space:install_write_tap(0xe0274, 0xe0275, "pass", function(offset, data, mask)
    pass_time = now(); pass_frame = screen:frame_number(); vram_writes = 0
    -- Colours the palette stepper (world_palette_step) will process this
    -- pass: faded colours (animated banks, skipping finished mode-0 colours)
    -- and copied colours (one-shot banks). The tap sees the new counter.
    fade[0], fade[1], fade[2], copy_colours = 0, 0, 0, 0
    local counter = data & 0xffff
    for bank = 0, 63 do
      local job = 0xe0000 + 0x9e06 + 8 * bank
      local flags = space:read_u8(job + 2)
      if flags & 0x80 ~= 0 then
        local mode = flags & 3
        local speed = 0x121d + 4 * (space:read_u8(job + 4) & 15)
        if mode ~= 3 and counter & space:read_u16(speed + 2) == 0 then
          local base = space:read_u16(job)
          for colour = 0, 15 do
            local target = space:read_u16(0xa0000 + ((0xa212 + 32 * base + 2 * colour) & 0xffff))
            local current = space:read_u16(0xf8800 + 32 * bank + 2 * colour)
            if not (mode == 0 and current == target) then fade[mode] = fade[mode] + 1 end
          end
        end
      elseif flags & 0x40 ~= 0 then
        copy_colours = copy_colours + 16
      end
    end
  end),
  space:install_write_tap(0xd0000, 0xdffff, "vram", function(offset, data, mask)
    vram_writes = vram_writes + 1
  end),
  space:install_write_tap(0xeb892, 0xeb893, "idle", function(offset, data, mask)
    if not pass_time then return end
    local pc = pc_state.value
    if pc < 0x84d or pc > 0x85b then return end
    local cycles = math.floor((now() - pass_time) * 9000000 + 0.5)
    out:write(string.format("%d\t%d\t%d\t%04x\t%d\t%d\t%s\t%d\t%04x %04x %04x %04x %04x %04x\t%s\t%d\n", pass_frame, cycles,
      screen:frame_number() - pass_frame, r16(0x80), r16(0x84), (0x100 - (space:read_u8(0xe8b26))) & 0xff, handlers(), vram_writes,
      r16(0x28c), r16(0x262), r16(0xb898), r16(0x2a6), r16(0x292), r16(0x298), fade[0] .. " " .. fade[1] .. " " .. fade[2], copy_colours))
    pass_time = nil
  end),
}
local frame = 0
local dirs = { {"right"}, {"right"}, {"right", "down"}, {"right"}, {"right", "up"}, {"right"} }
local sweep = { {"right", "up"}, {"right"}, {"right", "down"}, {"right"} }
subscription = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  for _, f in pairs(inputs) do f:set_value(0) end
  for _, f in pairs(p2) do f:set_value(0) end
  if duo and frame >= 300 and frame < 306 then p2.coin:set_value(1) end
  if duo and ((frame >= 420 and frame < 426) or (frame >= 600 and frame < 606)) then p2.start:set_value(1) end
  if frame >= 300 and frame < 306 then inputs.coin:set_value(1) end
  if (frame >= 420 and frame < 426) or (frame >= 600 and frame < 606) then inputs.start:set_value(1) end
  if frame >= 2040 then
    local set = frame < 15000 and dirs[(frame - 2040) // 40 % 6 + 1] or sweep[(frame - 2040) // 150 % 4 + 1]
    for _, k in ipairs(set) do inputs[k]:set_value(1) end
    if duo then
      for _, k in ipairs(set) do p2[k]:set_value(1) end
      if frame % 1500 < 6 then p2.coin:set_value(1) end
      if frame % 300 >= 150 and frame % 300 < 156 then p2.start:set_value(1) end
      if (frame - 2040) % 24 < 6 then p2.b1:set_value(1) end
      if (frame - 2040) % 24 >= 12 and (frame - 2040) % 24 < 18 then p2.b2:set_value(1) end
    end
    if frame % 1500 < 6 then inputs.coin:set_value(1) end
    if frame % 300 >= 150 and frame % 300 < 156 then inputs.start:set_value(1) end
    local phase = (frame - 2040) % 24
    if phase < 6 then inputs.b1:set_value(1) end
    if phase >= 12 and phase < 18 then inputs.b2:set_value(1) end
  end
  if frame >= frames then out:close(); manager.machine:exit() end
end)
