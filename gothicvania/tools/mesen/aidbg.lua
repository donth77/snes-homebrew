-- Debug: at a settled frame, dump ALL enemy OAM slots + grab a screenshot to zoom into the skeleton.
local frame=0
emu.addEventCallback(function()
  if frame>=52 and frame<=57 then emu.setInput({start=true},0) else emu.setInput({},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==165 then
    local f=io.open("/tmp/aidbg.png","wb"); f:write(emu.takeScreenshot()); f:close()
    local o=io.open("/tmp/aidbg.txt","w")
    o:write(string.format("camX=%d  playerFeetX=%d\n",
      emu.read16(0x7F0000,emu.memType.snesMemory), emu.read16(0x7E2000,emu.memType.snesMemory)))
    for _,e in ipairs({0,3,4,5,6}) do
      local b=e*4
      local x=emu.read(b,emu.memType.snesSpriteRam); local y=emu.read(b+1,emu.memType.snesSpriteRam)
      local t=emu.read(b+2,emu.memType.snesSpriteRam); local a=emu.read(b+3,emu.memType.snesSpriteRam)
      -- high OAM table: 2 bits per sprite (x-high, size); entry e -> byte 512 + e//4, bits (e%4)*2
      local hi=emu.read(512 + (e>>2), emu.memType.snesSpriteRam)
      local sz=(hi>>(((e&3)*2)+1))&1
      o:write(string.format("entry %d: X=%d Y=%d tile=%d attr=0x%02X (nameHi=%d pal=%d prio=%d hflip=%d) size=%d\n",
        e,x,y,t,a,a&1,(a>>1)&7,(a>>4)&3,(a>>6)&1,sz))
    end
    o:close(); emu.stop(0)
  end
end, emu.eventType.startFrame)
