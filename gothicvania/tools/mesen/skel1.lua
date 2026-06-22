-- Milestone 1: verify the two static test skeletons render + the >=256 tile-name path (slot 2 = name 320).
local CAMX=0x7F0000
local frame=0
local function save(p) local f=io.open(p,"wb"); if f then f:write(emu.takeScreenshot()); f:close() end end
local function oam(entry)
  local b=entry*4
  return emu.read(b+0,emu.memType.snesSpriteRam), emu.read(b+1,emu.memType.snesSpriteRam),
         emu.read(b+2,emu.memType.snesSpriteRam), emu.read(b+3,emu.memType.snesSpriteRam)
end
emu.addEventCallback(function()
  if frame>=48 and frame<=54 then emu.setInput({start=true},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if frame==150 then
    save("/tmp/skel1.png")
    local out=io.open("/tmp/skel1.txt","w")
    local cam=emu.read16(CAMX,emu.memType.snesMemory)
    out:write(string.format("camX=%d\n", cam))
    for _,e in ipairs({0,2,3,5}) do
      local x,y,t,a=oam(e)
      out:write(string.format("OAM entry %d (id %d): X=%d Y=%d tile=%d attr=0x%02X (nameHi=%d pal=%d prio=%d)\n",
        e, e*4, x, y, t, a, a&1, (a>>1)&7, (a>>4)&3))
    end
    out:close()
    emu.stop(0)
  end
end, emu.eventType.startFrame)
