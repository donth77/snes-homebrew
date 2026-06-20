local FX,FY,OG = 0x7E2000,0x7E2004,0x7F0006
local frame=0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
local function logp(tag)
  local px=emu.read16(FX+1,emu.memType.snesMemory)
  local py=emu.read16(FY+1,emu.memType.snesMemory)
  local og=emu.read(OG,emu.memType.snesMemory)
  local f=io.open("/tmp/play.txt","a"); f:write(string.format("%s feetX=%d feetY=%d onGround=%d\n",tag,px,py,og)); f:close()
end
emu.addEventCallback(function()
  if frame>=60 and frame<240 then emu.setInput({right=true},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==55 then save("/tmp/gv_land.png"); logp("landed") end
  if frame==240 then save("/tmp/gv_walk.png"); logp("walked"); emu.stop(0) end
end, emu.eventType.startFrame)
