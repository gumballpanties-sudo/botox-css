#include "unitE_menu.h"
#include "menu.h"   // MenuSection / MenuGroupToggle -- shared, exported wrappers only

// Every widget unit E used to draw here (hit marker, death particles, score popup,
// spectator list, watermark style, media player, discord rpc, console color, debug
// menu) now lives inline in menu.cpp's Page_MiscMain -- botox's own tab_misc is one
// function, not a unit-per-row call chain, so the botox group boxes/cogs for "overlays"
// and "game" needed single-function ownership of their checkboxes. Kept as a no-op so
// the UNIT_MENU_SLOT call site stays valid.
void UnitE_Menu_misc_main()
{
}
