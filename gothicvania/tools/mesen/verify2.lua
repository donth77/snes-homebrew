local FX,FY,OG=0x7E2000,0x7E2004,0x7F0006
local frame=0
local function logp(t) local f=io.open("/tmp/v2.txt","a"); f:write(string.format("%-16s f=%d feetX=%d feetY=%d onGround=%d\n",t,frame,emu.read16(FX+1,emu.memType.snesMemory),emu.read16(FY+1,emu.memType.snesMemory),emu.read(OG,emu.memType.snesMemory))); f:close() end
emu.addEventCallback(function()
  local inp={}
  -- phase A: attack while holding right (movement should be LOCKED)
  if frame>=50 and frame<95 then inp.right=true end
  if frame==60 then inp.y=true end
  -- phase B: a jump (measure height)
  if frame==130 then inp.b=true end
  emu.setInput(inp,0)
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==58 then logp("pre-attack") end
  if frame==70 then logp("mid-attack") end     -- feetX should == pre-attack-ish (locked)
  if frame==85 then logp("attack-ending") end
  if frame==92 then logp("post-attack") end     -- feetX moves again
  if frame==150 then logp("jump-peak?") end     -- min feetY
  if frame==300 then emu.stop(0) end
end, emu.eventType.startFrame)
