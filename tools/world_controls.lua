-- Let the initial reset and the optional hybrid soft reset finish before opening the menu.
local menu_pending = true
emu.register_frame_done(function()
    if menu_pending then
        menu_pending = false
        manager.ui:show_menu()
        manager.machine:popmessage("Input Settings > Input Assignments (this system)\nArrows move, Enter selects, Esc goes back.")
    end
end)
