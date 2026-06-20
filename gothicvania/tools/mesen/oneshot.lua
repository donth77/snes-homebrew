local frame=0
emu.addEventCallback(function() if frame>=20 then emu.setInput({right=true},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==60 then local f=io.open("/tmp/hero_after.png","wb"); f:write(emu.takeScreenshot()); f:close(); emu.stop(0) end
end, emu.eventType.startFrame)
