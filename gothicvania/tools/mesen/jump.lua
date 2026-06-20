local FY,OG=0x7E2004,0x7F0006
local frame, minY = 0, 999
local function logp(t) local f=io.open("/tmp/jump.txt","a"); f:write(string.format("%-10s f=%d feetY=%d onGround=%d\n",t,frame,emu.read16(FY+1,emu.memType.snesMemory),emu.read(OG,emu.memType.snesMemory))); f:close() end
emu.addEventCallback(function() if frame==50 then emu.setInput({b=true},0) else emu.setInput({},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  local y=emu.read16(FY+1,emu.memType.snesMemory)
  if frame>=50 and y<minY then minY=y end
  if frame==48 then logp("ground") end
  if frame==62 then logp("rising") end
  if frame==78 then logp("near-peak") end
  if frame==95 then logp("falling") end
  if frame==120 then logp("landed?") end
  if frame==130 then local f=io.open("/tmp/jump.txt","a"); f:write("PEAK feetY="..minY.." (jump height = "..(192-minY).."px)\n"); f:close(); emu.stop(0) end
end, emu.eventType.startFrame)
