-- round 2 demo: head hitbox position (bridged through aim/esp's own SetupBones call),
-- class name/id, and a sparks burst on a keypress. hitbox 12 = head (MEMORY ref-css-weapon-
-- spread-slots), same constant the aimbot's own hitbox picker uses.
local enabled = ui.checkbox("head markers", true)
local spark_key = ui.keybind("spark at head", 0x46, 1) -- VK_F = 0x46

callbacks.register("paint", function()
    if not enabled:get() or not engine.in_game() then return end

    for _, player in ipairs(entities.players(false, false)) do
        local hx, hy, hz = player:hitbox_position(12)
        if hx then
            local sx, sy = render.world_to_screen(hx, hy, hz)
            if sx then
                render.circle(sx, sy, 4, color(255, 60, 60, 255), 12, 1.5)
            end

            if spark_key:get() then
                fx.sparks(hx, hy, hz)
            end
        end
    end
end)

callbacks.register("game_event", function(e)
    if e:name() ~= "player_death" then return end
    local victim = e:get_player("userid")
    if victim then
        print(victim:name(), "was", victim:class_name(), "class id", victim:class_id())
    end
end)
