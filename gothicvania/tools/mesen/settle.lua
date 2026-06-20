local frame=0
emu.addEventCallback(function() end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==70 then local f=io.open("/tmp/gv_start.png","wb"); f:write(emu.takeScreenshot()); f:close(); emu.stop(0) end
end, emu.eventType.startFrame)
