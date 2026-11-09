-- MAME 0.289 diagnostic capture. The native renderer consumes board state,
-- while screen:pixels() supplies the independent reference image.
local machine = manager.machine
local program = machine.devices[":maincpu"].spaces.program
local items = machine.devices[":"].items
local function state(name)
    local index = assert(items[name], "Missing World board state: " .. name)
    return emu.item(index)
end
local frame, previous_sprites, previous_tiles, previous_controls = 0, nil, nil, nil
render_capture_subscription = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    if frame == world_capture_frame - 1 then
        previous_sprites = state("0/:spriteram_buffer"):read_block(0, 2048)
        previous_tiles = program:read_range(0xd0000, 0xdffff, 8)
        previous_controls = {}
        local master = state("0/m_pf_master_control")
        for n = 0, 2 do
            local control = state(n .. "/layer->control")
            table.insert(previous_controls, string.pack("<I2I2I2", master:read(n), control:read(0), control:read(2)))
        end
        table.insert(previous_controls, string.pack("<I2", state("0/m_videocontrol"):read(0)))
    elseif frame == world_capture_frame then
        local out = assert(io.open(world_capture_file, "wb"))
        assert(out:write("WRV1", table.concat(previous_controls)))
        assert(out:write(previous_tiles))
        assert(out:write(previous_sprites))
        assert(out:write(state("0/m_paletteram_buffer"):read_block(0, 16384)))
        local pixels, width, height = machine.screens[":screen"]:pixels()
        assert(width == 320 and height == 240)
        assert(out:write(pixels))
        assert(out:close())
        machine:exit()
    end
end)


