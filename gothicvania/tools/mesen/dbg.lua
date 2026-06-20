local CAMX=0x7F0000
local frame=0
local function dump(path)
  local f=io.open(path,"w")
  f:write("camX="..emu.read16(CAMX,emu.memType.snesMemory).."\n")
  f:write("slotA")
  for i=0,1023 do f:write(","..emu.read16(0xD000+i*2,emu.memType.snesVideoRam)) end
  f:write("\nslotB")
  for i=0,1023 do f:write(","..emu.read16(0xD800+i*2,emu.memType.snesVideoRam)) end
  f:write("\n"); f:close()
end
emu.addEventCallback(function()
  if frame>=30 and frame<1200 then emu.setInput({right=true},0)
  elseif frame>=1200 then emu.setInput({left=true},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==30 then dump("/tmp/vram_early.txt") end
  if frame==2380 then dump("/tmp/vram_late.txt"); emu.stop(0) end
end, emu.eventType.startFrame)
