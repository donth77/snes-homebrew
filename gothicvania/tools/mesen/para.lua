local f=0
local function shot(n) local p=emu.takeScreenshot(); local h=io.open(n,"wb"); h:write(p); h:close() end
emu.addEventCallback(function() if f>40 then emu.setInput({right=true},0) else emu.setInput({},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  f=f+1
  if f==45 then shot("/tmp/p_start.png") end
  if f==620 then shot("/tmp/p_scrolled.png"); emu.stop(0) end
end, emu.eventType.startFrame)
