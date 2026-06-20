local CX=0x7F0000
local s={}
local function vsum(byte0,n)
  local t=0
  for i=byte0,byte0+n-1 do t=t+emu.read(i, emu.memType.snesVideoRam) end
  return t
end
local f=0
emu.addEventCallback(function() emu.setInput({right=true},0) end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  f=f+1
  if f==120 then s.h1=vsum(0,4096) end                  -- full hero band
  if f==240 then s.h2=vsum(0,4096) end
  if f==300 then s.bgA=vsum(0xD000,2048); s.bgB=vsum(0xD800,2048) end  -- BG tilemap both slots
  if f==360 then
    local o=io.open('/tmp/stream.txt','w')
    o:write('heroBand f120='..s.h1..' f240='..s.h2..' animating='..tostring(s.h1~=s.h2 and s.h1>0)..'\n')
    o:write('BGslotA='..s.bgA..' BGslotB='..s.bgB..' (both>0 = pages resident)\n')
    o:write('camX='..emu.read16(CX,emu.memType.snesMemory)..'\n')
    o:close(); emu.stop(0)
  end
end, emu.eventType.startFrame)
