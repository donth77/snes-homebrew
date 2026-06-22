local frame=0
local function nz(addr,n)  -- count non-zero bytes in n bytes from addr (snesVideoRam)
  local c=0; for k=0,n-1 do if emu.read(addr+k,emu.memType.snesVideoRam)~=0 then c=c+1 end end; return c
end
emu.addEventCallback(function()
  if frame>=52 and frame<=57 then emu.setInput({start=true},0) else emu.setInput({},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==120 then
    local o=io.open("/tmp/vram2.txt","w")
    -- calibrate: moon data is at VRAM word 0x0800. Try both byte-addressed (2*word) and word-addressed.
    o:write(string.format("CALIB moon: @byte0x1000 nz=%d/512   @0x0800 nz=%d/512\n", nz(0x1000,512), nz(0x0800,512)))
    -- enemy slot 0 word 0x1000 (-> byte 0x2000); slot 2 word 0x1800 (-> byte 0x3000)
    o:write(string.format("slot0 (word0x1000): @byte0x2000 nz=%d/512   @0x1000 nz=%d/512\n", nz(0x2000,512), nz(0x1000,512)))
    o:write(string.format("slot2 (word0x1800): @byte0x3000 nz=%d/512   @0x1800 nz=%d/512\n", nz(0x3000,512), nz(0x1800,512)))
    o:close(); emu.stop(0)
  end
end, emu.eventType.startFrame)
