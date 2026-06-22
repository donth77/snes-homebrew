-- VRAM map: non-zero count per 0x800-byte chunk across 0..0x10000, at a settled AI frame. Byte-addressed
-- (2*word): hero=byte 0x0000-0x1000, moon=0x1000-0x2000, enemy=0x2000-0x4000, BG2=0x4000-0x7000.
local frame=0
emu.addEventCallback(function() if frame>=52 and frame<=57 then emu.setInput({start=true},0) else emu.setInput({},0) end end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==170 then
    local o=io.open("/tmp/vmap.txt","w")
    for c=0,31 do
      local base=c*0x800; local n=0
      for k=0,0x7FF do if emu.read(base+k,emu.memType.snesVideoRam)~=0 then n=n+1 end end
      o:write(string.format("chunk %2d  byte 0x%04X  nz=%d\n", c, base, n))
    end
    -- the skeleton OAM
    o:write(string.format("OAM e3: X=%d Y=%d tile=%d hi=%d\n",
      emu.read(12,emu.memType.snesSpriteRam),emu.read(13,emu.memType.snesSpriteRam),
      emu.read(14,emu.memType.snesSpriteRam),emu.read(15,emu.memType.snesSpriteRam)&1))
    o:close(); emu.stop(0)
  end
end, emu.eventType.startFrame)
