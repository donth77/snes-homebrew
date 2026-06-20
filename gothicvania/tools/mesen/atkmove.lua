local FX=0x7E2000
local frame=0
local function logp(t) local f=io.open("/tmp/atkmove.txt","a"); f:write(string.format("%s frame=%d feetX=%d\n",t,frame,emu.read16(FX+1,emu.memType.snesMemory))); f:close() end
emu.addEventCallback(function()
  local inp={}
  if frame>=50 then inp.right=true end   -- hold right the whole time
  if frame==60 then inp.y=true end        -- attack on frame 60 (while moving)
  emu.setInput(inp,0)
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==58 then logp("before-attack") end
  if frame==66 then logp("attacking") end
  if frame==74 then logp("attacking") end
  if frame==82 then logp("attack-ending") end
  if frame==90 then logp("after-attack"); emu.stop(0) end
end, emu.eventType.startFrame)
