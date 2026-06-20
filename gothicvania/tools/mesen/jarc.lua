local FY=0x7E2004
local frame, minY = 0, 999
emu.addEventCallback(function() if frame==80 then emu.setInput({b=true},0) else emu.setInput({},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  local y=emu.read16(FY+1,emu.memType.snesMemory)
  if frame>=80 and y<minY then minY=y end
  if frame>=80 and frame<=170 and frame%6==0 then local f=io.open("/tmp/jarc.txt","a"); f:write("f="..frame.." feetY="..y.."\n"); f:close() end
  if frame==180 then local f=io.open("/tmp/jarc.txt","a"); f:write("PEAK feetY="..minY.." height="..(192-minY).."px (demo=48px)\n"); f:close(); emu.stop(0) end
end, emu.eventType.startFrame)
