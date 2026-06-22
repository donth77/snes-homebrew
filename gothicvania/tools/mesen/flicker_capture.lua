-- GOTHICVANIA FLICKER CAPTURE  (run this in Mesen's Script Window)
-- No buttons needed. Just JUMP onto the grass platform where the ground flickers (do it a few times).
-- It auto-saves every airborne frame (screenshot + game state) to /tmp/play/.
local n, post = 0, 0
local log = io.open("/tmp/play/log.txt", "w")
local function r16(a) return emu.read16(a, emu.memType.snesMemory) end
local function r8(a)  return emu.read(a,  emu.memType.snesMemory) end
emu.addEventCallback(function()
  local og = r8(0x7F000F)                 -- onGround: 0 = airborne
  if og == 0 then post = 14 end           -- keep grabbing 14 frames past landing (catches the landing too)
  if post > 0 then
    post = post - 1
    n = n + 1
    if n <= 600 then
      local fx = (r16(0x7E2000) + r16(0x7E2002)*65536) / 256
      local fy = (r16(0x7E2004) + r16(0x7E2006)*65536) / 256
      log:write(string.format("f%03d feetX=%d feetY=%d camX=%d onGround=%d need=%d dmaHero=%d heroGfx=%d\n",
        n, math.floor(fx), math.floor(fy), r16(0x7F0000), og, r8(0x7F000C), r8(0x7F000D), r8(0x7F000E)))
      log:flush()
      local png = emu.takeScreenshot()
      if png then local h = io.open(string.format("/tmp/play/f%03d.png", n), "wb"); h:write(png); h:close() end
    end
  end
end, emu.eventType.startFrame)
emu.displayMessage("Capture", "ARMED - jump onto the platform that flickers")
