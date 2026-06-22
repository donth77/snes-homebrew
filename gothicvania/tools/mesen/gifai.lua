-- Milestone 3: skeleton AI. Enter play and stay PUT -- the spawner at tile 10 fires near the start, so a
-- skeleton rises ahead of the hero then shuffles left toward him. Capture a GIF + trace slot-0's OAM X
-- (entry 3) to confirm it moves toward the player (X should decrease as it walks left).
local frame=0
local out
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
emu.addEventCallback(function()
  if frame>=52 and frame<=57 then emu.setInput({start=true},0) else emu.setInput({},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame>=58 and frame<=200 then save(string.format("/tmp/gai_%03d.png",frame)) end
  if frame==70 or frame==110 or frame==150 or frame==190 then
    if not out then out=io.open("/tmp/gai_trace.txt","w") end
    local x=emu.read(3*4+0,emu.memType.snesSpriteRam)
    local y=emu.read(3*4+1,emu.memType.snesSpriteRam)
    local t=emu.read(3*4+2,emu.memType.snesSpriteRam)
    local a=emu.read(3*4+3,emu.memType.snesSpriteRam)
    out:write(string.format("frame %d: slot0 OAM X=%d Y=%d tile=%d hflip=%d\n",frame,x,y,t,(a>>6)&1))
  end
  if frame>200 then if out then out:close() end emu.stop(0) end
end, emu.eventType.startFrame)
