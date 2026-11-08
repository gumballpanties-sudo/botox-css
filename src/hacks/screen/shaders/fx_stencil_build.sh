#!/usr/bin/env bash
# Rebuilds fx_stencil_*.h from fx_stencil.hlsl (one header per entry point).
# Run from the project root:  bash src/hacks/screen/shaders/fx_stencil_build.sh
set -euo pipefail
if [ -z "${FXC:-}" ]; then
    FXC=$(ls -d "/c/Program Files (x86)/Windows Kits/10/bin/"*/x64/fxc.exe 2>/dev/null | sort -V | tail -1)
fi
[ -x "$FXC" ] || { echo "fxc not found, set FXC="; exit 1; }
DIR=src/hacks/screen/shaders
for e in fill rows blur glow inner capsule bone columns; do
    V="k_fxStencil$(tr '[:lower:]' '[:upper:]' <<< ${e:0:1})${e:1}Bytes"
    "$FXC" //nologo //T ps_3_0 //E ps_$e //Fh "$DIR/fx_stencil_$e.h" //Vn "$V" "$DIR/fx_stencil.hlsl" >/dev/null
done
"$FXC" //nologo //T vs_3_0 //E vs_capsule //Fh "$DIR/fx_stencil_vs.h" //Vn k_fxStencilVsBytes "$DIR/fx_stencil.hlsl" >/dev/null
echo "fx_stencil: ok"
