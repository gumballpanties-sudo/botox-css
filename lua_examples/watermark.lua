-- top right watermark: fps, ping, time (botox's original also showed the steam name --
-- this port's engine.player_info() doesn't carry a display name for the local player any
-- cheaper than entities.local_player():name(), so it's used here instead)
local enabled = ui.checkbox("show watermark", true)
local accent  = ui.color("accent", 150, 200, 60, 255)
local margin  = ui.slider_int("margin", 0, 50, 10, "%d px")

local fps = 0

callbacks.register("paint", function()
    if not enabled:get() then return end

    local frametime = globals.absframetime()
    if frametime > 0 then fps = fps * 0.9 + (1 / frametime) * 0.1 end

    local ping = 0
    if engine.in_game() then ping = engine.latency() end

    local text = string.format("botox-css | %d fps | %d ms | %s", math.floor(fps + 0.5), math.floor(ping * 1000), os.date("%H:%M:%S"))

    local w, h = render.text_size(text, fonts.default)
    local sw   = render.screen_size()
    local m    = margin:get()
    local x, y = sw - w - m - 8, m

    render.rect_filled(x, y, x + w + 8, y + h + 6, color(20, 20, 20, 220), 3)
    render.rect_filled(x, y, x + w + 8, y + 2, color(accent:get()))
    render.text(x + 4, y + 3, text, color(255, 255, 255), fonts.default)
end)
