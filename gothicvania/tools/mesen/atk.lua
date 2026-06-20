local frame,gif=0,0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function() if frame==60 then emu.setInput({y=true},0) else emu.setInput({},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame>=62 and frame<=86 and frame%3==0 then save(string.format("/tmp/atk_%02d.png",gif)); gif=gif+1 end
  if frame>86 then emu.stop(0) end
end, emu.eventType.startFrame)
