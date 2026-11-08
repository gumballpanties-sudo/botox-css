-- simple 2d box + name + health bar, built off the player's origin and eye position.
-- CS:S difference from the CS:GO original: entity:mins_maxs() is nil in this port (no
-- verified CS:S ICollideable OBB accessor outside aim/, see docs/lua_api.md) -- the box here
-- is built from origin (feet) to eye_position() scaled up slightly for the head margin,
-- not the real hull. Close enough for a box, not pixel-exact against the model.
local enabled   = ui.checkbox("boxes", true)
local teammates = ui.checkbox("teammates", false)
local box_color = ui.color("box color", 255, 255, 255, 255)
local style     = ui.combo("health text", { "off", "number", "percent" }, 2)

local function screen_box(player)
    local ox, oy, oz = player:origin()
    local ex, ey, ez = player:eye_position()
    if not ox or not ex then return nil end

    local top_x, top_y = render.world_to_screen(ex, ey, ez + 6)   -- a little above the eyes
    local bot_x, bot_y = render.world_to_screen(ox, oy, oz)
    if not top_x or not bot_x then return nil end

    local h = bot_y - top_y
    local w = h * 0.5
    return top_x - w / 2, top_y, top_x + w / 2, bot_y
end

callbacks.register("paint", function()
    if not enabled:get() or not engine.in_game() then return end

    local me = entities.local_player()

    for _, player in ipairs(entities.players()) do
        if player ~= me and (teammates:get() or player:is_enemy()) then
            local x1, y1, x2, y2 = screen_box(player)
            if x1 then
                render.rect(x1 - 1, y1 - 1, x2 + 1, y2 + 1, color(0, 0, 0, 160), 3)
                render.rect(x1, y1, x2, y2, color(box_color:get()))

                local name = player:name()
                local nw   = render.text_size(name, fonts.default)
                render.text((x1 + x2) / 2 - nw / 2, y1 - 14, name, color(255, 255, 255), fonts.default)

                local hp   = math.max(0, math.min(player:health(), 100))
                local fill = (y2 - y1) * hp / 100
                render.rect_filled(x1 - 6, y1 - 1, x1 - 2, y2 + 1, color(0, 0, 0, 160))
                render.rect_filled(x1 - 5, y2 - fill, x1 - 3, y2, color(math.floor(255 - hp * 2.55), math.floor(hp * 2.55), 0))

                local mode = style:get()
                if mode > 1 and hp < 100 then
                    local label = mode == 2 and tostring(hp) or (hp .. "%")
                    render.text(x1 - 6, y2 - fill - 6, label, color(255, 255, 255), fonts.default)
                end
            end
        end
    end
end)
