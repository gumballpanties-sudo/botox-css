#pragma once
// botox csgo hacks/visuals/screen/rear_mirror, CS:S x64 port: visuals > world "rear view mirror". CViewRender::RenderView
// renders the view turned 180 into an own target before the main view, a draggable rounded imgui box shows it.

void RearMirror_Install(void** viewSlot);   // thread.cpp: the `view` global (k_viewRenderSig), hooks CViewRender::RenderView
bool RearMirror_Drawing();                  // any hook under RenderView: true inside the mirror pass, botox passes stay out
void RearMirror_Draw();                     // render thread, BuildImGuiFrame
void RearMirror_DropTexture();              // h_Reset: the target's d3d texture is remade
