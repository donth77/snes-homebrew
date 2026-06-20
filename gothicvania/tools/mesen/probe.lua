-- probe: prove CLI script-loading + file IO work (no screen needed)
local ok = io.open("/tmp/lua_loaded.txt", "w")
if ok then ok:write("loaded"); ok:close() end
if emu and emu.log then emu.log("probe loaded") end
