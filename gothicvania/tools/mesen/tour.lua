local CAMX=0x7F0000
local frame, idx = 0, 1
local targets={0,1024}
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function() if frame>=45 then emu.setInput({right=true},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame<40 then return end
  local cam=emu.read16(CAMX,emu.memType.snesMemory)
  if idx<=#targets and cam>=targets[idx] then save(string.format("/tmp/gv_%02d.png",idx-1)); idx=idx+1 end
  if idx>#targets then emu.stop(0) end
end, emu.eventType.startFrame)
