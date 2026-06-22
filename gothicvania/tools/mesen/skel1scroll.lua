-- Milestone 1 scroll test: walk right; capture at two camX points to confirm the static skeletons track
-- the world + cull off-screen, and the scroll/streaming is unregressed (capture #2 is past a page seam).
local CAMX=0x7F0000
local frame, idx = 0, 1
local targets={260, 1120}
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function() if frame>=55 and frame<=60 then emu.setInput({start=true},0)
  elseif frame>60 then emu.setInput({right=true},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame<70 then return end
  local cam=emu.read16(CAMX,emu.memType.snesMemory)
  if idx<=#targets and cam>=targets[idx] then
    save(string.format("/tmp/skel1scroll_%d.png",idx)); idx=idx+1
  end
  if idx>#targets or frame>900 then emu.stop(0) end
end, emu.eventType.startFrame)
