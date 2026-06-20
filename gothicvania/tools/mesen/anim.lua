local FX,FY,OG=0x7E2000,0x7E2004,0x7F0006
local frame=0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
local function logp(t) local f=io.open("/tmp/anim.txt","a"); f:write(string.format("%s feetX=%d feetY=%d onGround=%d\n",t,emu.read16(FX+1,emu.memType.snesMemory),emu.read16(FY+1,emu.memType.snesMemory),emu.read(OG,emu.memType.snesMemory))); f:close() end
emu.addEventCallback(function()
  local inp={}
  if frame>=65 and frame<120 then inp.right=true end
  if frame==130 then inp.b=true end
  if frame>=160 and frame<210 then inp.down=true end
  if frame==214 then inp.y=true end
  emu.setInput(inp,0)
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==60  then save("/tmp/a_idle.png");  logp("idle")  end
  if frame==100 then save("/tmp/a_run.png");   logp("run")   end
  if frame==140 then save("/tmp/a_jump.png");  logp("jump")  end
  if frame==195 then save("/tmp/a_crouch.png");logp("crouch")end
  if frame==218 then save("/tmp/a_attack.png");logp("attack"); emu.stop(0) end
end, emu.eventType.startFrame)
