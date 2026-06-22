-- Milestone 2 GIF: enter play, walk right a little to frame both skeletons on screen, then idle so the
-- camera holds and the skeletons animate in place. Capture every frame (the encoder drops the blank
-- capture-lag frames) over the idle window.
local frame=0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function()
  if frame>=52 and frame<=57 then emu.setInput({start=true},0)
  elseif frame>=62 and frame<=92 then emu.setInput({right=true},0)
  else emu.setInput({},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame>=96 and frame<=170 then save(string.format("/tmp/gskel_%03d.png",frame)) end
  if frame>170 then emu.stop(0) end
end, emu.eventType.startFrame)
