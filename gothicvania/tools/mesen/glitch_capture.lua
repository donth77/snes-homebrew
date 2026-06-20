-- Gothicvania glitch capture (run in Mesen GUI: Debug > Script Window > open this > Run).
-- The game logs camX/feetX/input/stream every frame to a wrapping buffer and FREEZES it 24
-- frames after a scroll stall while moving (the input-freeze signature). It also freezes on
-- SELECT, so if you SEE a glitch and nothing auto-captured, press SELECT right then.
-- When frozen, this dumps /tmp/glitch.csv and shows a message. Then tell Claude.
local CAMX,FEET,FLAG = 0x7E200C, 0x7E240C, 0x7E280C
local HEAD,COUNT,DONE = 0x7F000A, 0x7F000C, 0x7F0012
local M = emu.memType.snesMemory
local done = false
local function chk()
  if done then return end
  if emu.read(DONE, M) == 0 then return end
  local cnt  = emu.read16(COUNT, M)
  local head = emu.read16(HEAD, M)
  local n = cnt; if n > 512 then n = 512 end
  local f = io.open("/tmp/glitch.csv", "w")
  f:write("k,camX,feetX,right,left,ground,jumpdir,need,herodma\n")
  for k = 0, n-1 do
    local idx = k
    if cnt > 512 then idx = (head + k) % 512 end
    local cam = emu.read16(CAMX + idx*2, M)
    local fe  = emu.read16(FEET + idx*2, M)
    local fl  = emu.read(FLAG + idx, M)
    f:write(string.format("%d,%d,%d,%d,%d,%d,%d,%d,%d\n", k, cam, fe,
      fl%2, math.floor(fl/2)%2, math.floor(fl/4)%2, math.floor(fl/8)%2,
      math.floor(fl/16)%2, math.floor(fl/32)%2))
  end
  f:close(); done = true
  emu.displayMessage("CAPTURE", "Glitch captured -> /tmp/glitch.csv  (tell Claude)")
end
emu.addEventCallback(chk, emu.eventType.startFrame)
emu.displayMessage("CAPTURE", "Recording. Reproduce the glitch (or press SELECT the instant you see it).")
