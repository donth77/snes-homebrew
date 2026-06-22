-- Read VRAM at enemy slot 0 (word 0x1000) at a settled walk frame, dump the 8 grid-rows' first tile so we
-- can tell if the per-row DMA landed a real skeleton there (non-zero) or blanks.
local frame=0
emu.addEventCallback(function()
  if frame>=52 and frame<=57 then emu.setInput({start=true},0) else emu.setInput({},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==150 then
    local o=io.open("/tmp/vramdbg.txt","w")
    -- entry 3 (slot 0) OAM
    o:write(string.format("slot0 OAM X=%d Y=%d tile=%d\n",
      emu.read(12,emu.memType.snesSpriteRam),emu.read(13,emu.memType.snesSpriteRam),emu.read(14,emu.memType.snesSpriteRam)))
    -- VRAM is word-addressed; emu.read on snesVideoRam takes a BYTE address. slot0 word 0x1000 -> byte 0x2000.
    for r=0,7 do
      local wbase = 0x1000 + r*256       -- grid-row r, slot 0 left cols
      local bbase = wbase*2
      local nz=0; local s=""
      for t=0,7 do                        -- 8 tiles of this row, 32 bytes each
        local tnz=0
        for k=0,31 do if emu.read(bbase + t*32 + k, emu.memType.snesVideoRam)~=0 then tnz=tnz+1 end end
        if tnz>0 then nz=nz+1 end
        s=s..(tnz>0 and "#" or ".")
      end
      o:write(string.format("row %d (word 0x%04X): %s  (%d/8 tiles non-blank)\n", r, wbase, s, nz))
    end
    o:close(); emu.stop(0)
  end
end, emu.eventType.startFrame)
