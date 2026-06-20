local frame=0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function() if frame>=60 and frame<150 then emu.setInput({right=true},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==55  then save("/tmp/v_start.png") end   -- hero idle + gravestone/cross
  if frame==150 then save("/tmp/v_deep.png"); emu.stop(0) end  -- scrolled (tree area)
end, emu.eventType.startFrame)
