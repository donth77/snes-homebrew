local CX=0x7F0000
local f=0
local function shot(n) local p=emu.takeScreenshot(); local h=io.open(n,"wb"); h:write(p); h:close() end
emu.addEventCallback(function() emu.setInput({right=true},0) end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  f=f+1
  if f==600  then shot("/tmp/d1.png") end
  if f==601  then shot("/tmp/d1b.png") end    -- consecutive frame: detect per-frame jitter
  if f==1600 then shot("/tmp/d2.png") end
  if f==1601 then shot("/tmp/d2b.png") end
  if f==2600 then
     shot("/tmp/d3.png")
     local o=io.open("/tmp/d.txt","w"); o:write("camX f600,1600,2600 logged below\n"); o:close()
     emu.stop(0)
  end
  if f==600 or f==1600 or f==2600 then
     local o=io.open("/tmp/d.txt","a"); o:write("f="..f.." camX="..emu.read16(CX,emu.memType.snesMemory).."\n"); o:close()
  end
end, emu.eventType.startFrame)
