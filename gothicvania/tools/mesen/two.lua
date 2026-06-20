local frame=0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function()
  local inp={}
  if frame>=60 and frame<120 then inp.right=true end
  if frame>=135 and frame<185 then inp.left=true end
  if frame==205 then inp.y=true end
  emu.setInput(inp,0)
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==55  then save("/tmp/t_idle.png")  end
  if frame==110 then save("/tmp/t_runR.png")  end
  if frame==170 then save("/tmp/t_runL.png")  end
  if frame==214 then save("/tmp/t_atk.png"); emu.stop(0) end
end, emu.eventType.startFrame)
