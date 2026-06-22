-- Milestone 2: verify skeletons ANIMATE and there's NO DMA-overrun flicker. Walk right (hero anim +
-- scrolling + streaming) while the skeletons animate, and grab a burst of CONSECUTIVE frames -- an
-- overrun would corrupt a BG frame. Also dump skeleton OAM tile names at a couple of points.
local frame=0
local shot=0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function()
  if frame>=55 and frame<=60 then emu.setInput({start=true},0)
  elseif frame>60 then emu.setInput({right=true},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  -- burst of 12 consecutive frames once the hero is mid-walk (camX moving, skeletons still on screen)
  if frame>=92 and frame<=103 then
    shot=shot+1
    save(string.format("/tmp/anim_%02d.png",shot))
  end
  if frame==104 then emu.stop(0) end
end, emu.eventType.startFrame)
