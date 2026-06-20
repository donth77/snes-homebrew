local CX=0x7F0000
local f=0
local function shot(n)
  local p=emu.takeScreenshot(); local fh=io.open(n,"wb"); fh:write(p); fh:close()
end
emu.addEventCallback(function() if f>40 then emu.setInput({right=true},0) else emu.setInput({},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  f=f+1
  if f==40 then shot("/tmp/tl_start.png") end
  if f==560 then shot("/tmp/tl_mid.png") end
  if f==820 then
    shot("/tmp/tl_tree.png")
    local o=io.open("/tmp/tl.txt","w"); o:write("camX@820="..emu.read16(CX,emu.memType.snesMemory).."\n"); o:close()
    emu.stop(0)
  end
end, emu.eventType.startFrame)
