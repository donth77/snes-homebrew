local frame,gif=0,0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function()
  local inp={}
  if frame>=40 and frame<95 then inp.right=true end
  if frame==105 then inp.b=true end
  if frame>=130 and frame<175 then inp.left=true end
  if frame==195 or frame==220 then inp.y=true end
  emu.setInput(inp,0)
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame>=12 and frame<=250 and frame%4==0 then save(string.format("/tmp/gif_%03d.png",gif)); gif=gif+1 end
  if frame>250 then emu.stop(0) end
end, emu.eventType.startFrame)
