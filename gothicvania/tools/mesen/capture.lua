local CAMX = 0x7F0000
local frame, idx = 0, 1
local shots = {20, 600, 1200, 1500, 1900, 2380}
local log = io.open("/tmp/gv_cam.txt","w")
local function save(p,d) local f=io.open(p,"wb"); if f then f:write(d); f:close() end end
emu.addEventCallback(function()
  if frame>=20 and frame<1200 then emu.setInput({right=true},0)
  elseif frame>=1200 then emu.setInput({left=true},0) end
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame=frame+1
  if idx<=#shots and frame>=shots[idx] then
    local cam = emu.read16(CAMX, emu.memType.snesMemory)
    save(string.format("/tmp/gv_%02d.png",idx-1), emu.takeScreenshot())
    log:write(string.format("%d,%d,%d\n", idx-1, frame, cam)); log:flush()
    idx=idx+1
  end
  if frame>shots[#shots]+15 then log:close(); emu.stop(0) end
end, emu.eventType.startFrame)
