# SPDX-License-Identifier: MPL-2.0
# The build's environment (sourced by the scripts in tools/): the JieLi toolchain, the AC79 SDK, and on macOS
# a libfribidi for Pillow's text shaping (fm1-base's font generator needs it: see BUILDING.md).
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
for d in "$HOME/.local/fribidi/lib" /opt/homebrew/opt/fribidi/lib /usr/local/opt/fribidi/lib; do
    [ -d "$d" ] && export DYLD_LIBRARY_PATH="$d${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" && break
done
[ -f "$HOME/emsdk/emsdk_env.sh" ] && ! command -v emcc >/dev/null 2>&1 && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
true
