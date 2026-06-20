local frame,gif=0,0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function() if frame>=40 then emu.setInput({right=true},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame>=80 and frame<=110 and frame%2==0 then save(string.format("/tmp/run_%02d.png",gif)); gif=gif+1 end
  if frame>110 then emu.stop(0) end
end, emu.eventType.startFrame)
