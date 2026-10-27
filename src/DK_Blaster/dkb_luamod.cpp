// DK_Blaster: the DKB Lua table inside the game's Lua state (gun prop, saber, bolts, rays, safety state).
// Runs only through dk::lua (engine lock + cpcall + pcall): an error here returns "error: ..." and never reaches the
// game's strict _G handler. The table lives in _G (rawset) and survives level loads; rens it holds do not, so native
// code calls DKB.reset() when the player ren or the world changes.
#include "dkb.h"
#include <stdio.h>
#include <stdarg.h>

namespace dkb {

static const char* kModule = R"LUA(
local D = rawget(_G, 'DKB') or {}
D.ver = 0   -- set by the DLL after loading: a hash of this text
local V = math.Vector
local function P() return ren.GetPlayer(0) end
local function valid(r) if not r then return false end local ok, v = pcall(ren.IsValid, r) return ok and v end
local function rid(r) if not r then return '0' end return string.match(tostring(r), '%x+$') or '0' end

-- why blaster mode cannot run now ('ok' = it can)
function D.state()
  local p = P()
  if not p then return 'noplayer' end
  if game.IsDead(p) then return 'dead' end
  if ronin.InputIsMapOnStack('playerMiniGame', 0) then return 'qte' end
  local ok, s = pcall(player.GetSyncedWithID, 0)
  if ok and s then return 'sync' end
  if edl.IsCinematicPlaying() and not edl.IsInYourEarEDL() then return 'cine' end   -- radio dialogue: still playing
  local m = camera.GetPositionMode()
  if m ~= 'SmartFollow' and m ~= 'Strafe' then return 'cam' end
  return 'ok'
end

function D.reset() D.p = nil D.gun = nil D.tip = nil D.hilt = nil D.saberOff = nil D.nade = nil D.scoped = nil D.jets = nil end

local function dropGun()
  if D.gun then
    if D.p and valid(D.p) then pcall(attachment.Detach, D.p, D.gun) end
    if valid(D.gun) then pcall(ren.MakeInvisible, D.gun, true) pcall(ren.Destroy, D.gun) end
  end
  D.gun = nil D.tip = nil
end

-- A cinematic's end hands the saber back: the cutscene restore calls player.CinematicSaberEndAll (the blade back under
-- the game's control) and restores the rens' visibility. Radio dialogue runs that restore while blaster mode stays
-- armed, and the blade lit up in the next fight. Armed, the saber goes away again right after it.
function D.rehide()
  local p = D.p
  if not (D.saberOff and p and valid(p)) then return 'idle' end
  pcall(player.CinematicSaberStart, p, false)   -- already in cinematic mode: it only sets the flags again
  local h = ai.GetMeleeWeapon(p) or D.hilt
  if h then pcall(ren.MakeInvisible, h, true) D.hilt = h end
  return 'ok'
end

function D.afterEndAll()
  if D.saberOff and not (edl.IsCinematicPlaying() and not edl.IsInYourEarEDL()) and D.rehide() == 'ok' then
    D.cineFix = (D.cineFix or 0) + 1
  end
end

local function hookEndAll()
  local t = rawget(_G, 'player')
  local f = type(t) == 'table' and rawget(t, 'CinematicSaberEndAll')
  if not f or f == D.endAllWrap then return end
  D.endAllOrig = f
  D.endAllWrap = D.endAllWrap or function(...)   -- kept across module reloads: one wrapper, never stacked
    D.endAllOrig(...)
    pcall(D.afterEndAll)   -- runs inside the game's cinematic script: no error of ours may reach it
  end
  rawset(t, 'CinematicSaberEndAll', D.endAllWrap)
end

-- armed, 4 times a second: the EndAll guard in place, the hilt still hidden; returns the saber fixes since the last
-- call (cinematic ends, hilt shown again)
function D.guard()
  hookEndAll()
  local p = D.p
  if not (D.saberOff and p and valid(p)) then return 0, 0 end
  local n = D.cineFix or 0
  D.cineFix = 0
  local h = ai.GetMeleeWeapon(p) or D.hilt
  local ok, vis = false, false
  if h then ok, vis = pcall(ren.IsVisible, h) end
  if ok and vis then D.rehide() return n, 1 end
  return n, 0
end

-- saber away (blade off, hilt hidden) and the gun prop into the right hand
function D.arm(path)
  local p = P()
  if not p then return 'noplayer' end
  if D.p and D.p ~= p then D.reset() end
  D.p = p
  local h = ai.GetMeleeWeapon(p)
  D.hilt = h
  if not D.saberOff then
    pcall(player.CinematicSaberStart, p, false)
    D.saberOff = true
  end
  if h then pcall(ren.MakeInvisible, h, true) end
  hookEndAll()
  return D.setgun(path)
end

function D.setgun(path)
  local p = D.p or P()
  if not p then return 'noplayer' end
  dropGun()
  local ok, g = pcall(ren.LoadActor, path)
  if not ok or not g then return 'gunload' end
  pcall(physics.SetCollisionFlags, g, 0)
  local a = attachment.AddPoint(p, 'DKGun', 'rWeapon[0]', V(0, 0, 0), 0, 0, 0)
  local b = attachment.AttachToPoint(p, 'DKGun', g)
  D.gun = g
  if D.scoped then pcall(ren.MakeInvisible, g, true) end
  local okt, t = pcall(attachment.Find, g, 'Tip')
  if okt and t then D.tip = t end
  return (a and b) and 'ok' or 'attach'
end

-- back to the saber exactly as before (safe after the player ren changed or died)
-- aiming slows the player: the locomotion's own max speed (the game turns a part-pushed stick into a full run, so a
-- scaled stick never slowed anything); v = -1 gives the game's speed back
function D.movecap(v)
  local p = ren.GetPlayer(0)
  if p and valid(p) then pcall(game.OverrideLocomotionMaxSpeed, p, v) end
  D.capped = (v > 0) or nil
  return 'ok'
end

function D.disarm()
  dropGun()
  if D.capped then D.movecap(-1) end
  local p = D.p
  if p and valid(p) then
    if D.saberOff then pcall(player.CinematicSaberEnd, p) end
    local h = ai.GetMeleeWeapon(p) or D.hilt
    if h then pcall(ren.MakeInvisible, h, false) end
  end
  D.saberOff = nil D.hilt = nil
  return 'ok'
end

-- scoped: the gun prop is hidden, the lens shows only what is ahead (the scope camera sits in front of the player's
-- body, so he stays drawn and the AI sees and shoots him as usual); a gun drawn while scoped is hidden too
function D.scopehide(on)
  D.scoped = on
  if D.gun and valid(D.gun) then pcall(ren.MakeInvisible, D.gun, on) end
  return 'ok'
end

-- muzzle position (gun tip marker, else the right hand, else the chest)
function D.muzzle()
  local r = D.tip or D.gun or D.p
  if not r then return 0 end
  local ok, v = pcall(location.GetPosition, r)
  if not ok or not v then return 0 end
  if r == D.p then return 1, v.x, v.y + 1.35, v.z end
  return 1, v.x, v.y, v.z
end

-- one bolt; returns the projectile ren id (hex) or 0
function D.fire(ox, oy, oz, dx, dy, dz, spd, mt, grp, ent)
  local p = D.p or P()
  local ok, b = pcall(projectile.Launch, 0, V(ox, oy, oz), V(dx, dy, dz), spd, mt, p, nil, grp, ent)
  if ok and b then return rid(b) end
  return '0'
end

-- Characters are hit through per-bone capsules with gaps between them (a stormtrooper's neck: rays at the chin pass
-- between head and chest) and nothing over the top of the helmet; ray flag 8 is the whole-body envelope around them.
-- A ray (o -> t, direction f) inside a living AI's envelope that missed its capsules is snapped to the nearest body
-- part centre within reach (head centre 10 cm over neck[2]); rigs without these bones take the envelope point pushed
-- 15 cm in. Returns ren, x, y, z, part index (SNAP; 1 and 2 = head) or nil.
local SNAP = { { 'neck[2]', 0.10, 0.17 }, { 'neck[0]', 0.0, 0.10 }, { 'spine[3]', 0.0, 0.20 }, { 'spine[2]', 0.0, 0.20 },
  { 'spine[1]', 0.0, 0.20 }, { 'hips[0]', 0.0, 0.20 } }
-- body part of ren e whose surface the ray (direction f) hit at (x, y, z): the nearest centre within 0.8 x its radius
-- and not in front of the hit point. Arms and the rifle held across the chest are not parts: a hit there stays put.
local function part(e, x, y, z, fx, fy, fz)
  local bi, bx, by, bz, bd
  for i, s in ipairs(SNAP) do
    local ok, b = pcall(animation.GetBonePosWorld, e, s[1])
    if ok and b and (b.x ~= 0 or b.y ~= 0 or b.z ~= 0) then
      local cx, cy, cz = b.x, b.y + s[2], b.z
      local dx, dy, dz = cx - x, cy - y, cz - z
      local d = math.sqrt(dx * dx + dy * dy + dz * dz)
      if d < s[3] * 0.8 and dx * fx + dy * fy + dz * fz > -0.03 and (not bd or d < bd) then bi, bx, by, bz, bd = i, cx, cy, cz, d end
    end
  end
  return bi, bx, by, bz
end
local function snap(ox, oy, oz, tx, ty, tz, fx, fy, fz, maxd, p)
  local ok, r = pcall(physics.RayCast, V(ox, oy, oz), V(tx, ty, tz), p, 8)
  if not (ok and r and r.success and r.ren_id and r.dist and r.dist < maxd) then return nil end
  local e = r.ren_id
  local ok2, a = pcall(ai.IsAlive, e)
  if not (ok2 and a) then return nil end
  local bi, bx, by, bz, bd
  for i, s in ipairs(SNAP) do
    local ok3, b = pcall(animation.GetBonePosWorld, e, s[1])
    if ok3 and b and (b.x ~= 0 or b.y ~= 0 or b.z ~= 0) then
      local x, y, z = b.x, b.y + s[2], b.z
      local vx, vy, vz = x - ox, y - oy, z - oz
      local al = vx * fx + vy * fy + vz * fz
      if al > 0 then
        local px, py, pz = vx - fx * al, vy - fy * al, vz - fz * al
        local d = math.sqrt(px * px + py * py + pz * pz)
        if d < s[3] and (not bd or d < bd) then bi, bx, by, bz, bd = i, x, y, z, d end
      end
    end
  end
  if bd then return e, bx, by, bz, bi end
  local q = r.pos
  return e, q.x + fx * 0.15, q.y + fy * 0.15, q.z + fz * 0.15, 0
end

-- one trigger pull: camera ray from (e + f*s0) to the aim point, then one bolt per spread pair in sp (tangent
-- offsets along the camera right / up) from the muzzle toward it. Returns "id,id,", hit, living AI, aim distance.
function D.shoot(ex, ey, ez, fx, fy, fz, s0, range, rx, ry, rz, ux, uy, uz, spd, mt, grp, ent, cue, sp)
  local p = D.p or P()
  if not p then return '' end
  local tx, ty, tz = ex + fx * range, ey + fy * range, ez + fz * range
  local hit, live, hr, d1 = 0, 0, nil, range
  local ox, oy, oz = ex + fx * s0, ey + fy * s0, ez + fz * s0
  local ok, r = pcall(physics.RayCast, V(ox, oy, oz), V(tx, ty, tz), p, 0x10800)
  if ok and r and r.success then
    local q = r.pos
    tx, ty, tz, hit, hr, d1 = q.x, q.y, q.z, 1, r.ren_id, r.dist or range
    if r.ren_id then local ok2, a = pcall(ai.IsAlive, r.ren_id) if ok2 and a then live = 1 end end
  end
  -- the bolt leaves the muzzle half a metre below the camera and sees the target from another angle: it flies at the
  -- centre of the body part under the crosshair (a ray at the top of the head would clip the shoulders from the gun),
  -- and the part travels with the bolt so a head-aimed bolt that clips this target's chest still scores the head
  local gz, pi = 0, 0
  if live == 1 and hr and not D.noSnap then
    local i, x, y, z = part(hr, tx, ty, tz, fx, fy, fz)
    if i then tx, ty, tz, pi = x, y, z, i end
  elseif live == 0 and not D.noSnap then
    local e, x, y, z, i = snap(ox, oy, oz, tx, ty, tz, fx, fy, fz, d1 - 0.05, p)
    if e then tx, ty, tz, hit, live, hr, gz, pi = x, y, z, 1, 1, e, 1, i end
  end
  local headAim = (pi == 1 or pi == 2) and 1 or 0
  local okm, mx, my, mz = D.muzzle()
  if okm ~= 1 then mx, my, mz = ex + fx * s0, ey + fy * s0, ez + fz * s0 end
  -- the gun sees the aim point past arms and cover edges differently from the camera: when the line from the muzzle
  -- is blocked short of it, the bolt starts on the camera line just past the player (what you aim at is what you hit)
  local cam, br = 0, nil
  if okm == 1 and hit == 1 then
    local ax, ay, az = tx - mx, ty - my, tz - mz
    local al = math.sqrt(ax * ax + ay * ay + az * az)
    if al > 0.6 then
      local k = (al - 0.2) / al
      local ok3, r3 = pcall(physics.RayCast, V(mx, my, mz), V(mx + ax * k, my + ay * k, mz + az * k), p, 0x10800)
      if ok3 and r3 and r3.success then
        br = r3.ren_id
        mx, my, mz = ex + fx * (s0 + 0.6), ey + fy * (s0 + 0.6), ez + fz * (s0 + 0.6)
        cam = 1
      end
    end
  end
  D.last = string.format('o %.2f %.2f %.2f t %.2f %.2f %.2f s0 %.2f cam %d snap %d part %d hit %s block %s', mx, my, mz,
    tx, ty, tz, s0, cam, gz, pi, rid(hr), rid(br))
  local dx, dy, dz = tx - mx, ty - my, tz - mz
  local l = math.sqrt(dx * dx + dy * dy + dz * dz)
  -- aim point behind the muzzle (wall hugging): fire along the camera
  if l < 0.3 or (dx * fx + dy * fy + dz * fz) < 0 then dx, dy, dz, l = fx, fy, fz, 1 end
  dx, dy, dz = dx / l, dy / l, dz / l
  local ids = ''
  for i = 1, #sp, 2 do
    local a1, a2 = sp[i], sp[i + 1]
    local ok2, b = pcall(projectile.Launch, 0, V(mx, my, mz), V(dx + rx * a1 + ux * a2, dy + ry * a1 + uy * a2, dz + rz * a1 + uz * a2),
      spd, mt, p, nil, grp, ent)
    if ok2 and b then ids = ids .. rid(b) .. ',' end
  end
  if cue and cue ~= '' then pcall(audio.Play, V(mx, my, mz), cue) end
  return ids, hit, live, l, cam, headAim, live == 1 and rid(hr) or '0'
end

-- the engine plays one source per ren and cue: a cue played again on the same ren while it still rings is dropped
-- (fast fire kept 2 of 12 shots audible), so every sound is a new source at the ren's position
function D.sound(cue, onPlayer)
  local r = (not onPlayer and D.gun) or D.p or P()
  if not r then return end
  local ok, v = pcall(location.GetPosition, r)
  if ok and v then pcall(audio.Play, V(v.x, v.y + (r == D.gun and 0 or 1.2), v.z), cue) else pcall(audio.Play, r, cue) end
end

-- aim-camera boom against the static world (flag 1): free fraction of pivot -> shoulder, then of the boom vector
-- (bx,by,bz) from the clamped shoulder; m = margin kept from walls
local function freeFrac(x1, y1, z1, dx, dy, dz, m, p)
  local l = math.sqrt(dx * dx + dy * dy + dz * dz)
  if l < 0.01 then return 1 end
  local k = (l + m) / l
  local ok, r = pcall(physics.RayCast, V(x1, y1, z1), V(x1 + dx * k, y1 + dy * k, z1 + dz * k), p, 1)
  if ok and r and r.success and r.dist then
    local f = (r.dist - m) / l
    if f < 0 then f = 0 end
    if f > 1 then f = 1 end
    return f
  end
  return 1
end
function D.boom(px, py, pz, sx, sy, sz, bx, by, bz, m)
  local p = D.p or P()
  local f1 = freeFrac(px, py, pz, sx - px, sy - py, sz - pz, m, p)
  local qx, qy, qz = px + (sx - px) * f1, py + (sy - py) * f1, pz + (sz - pz) * f1
  return f1, freeFrac(qx, qy, qz, bx, by, bz, m, p)
end

-- thermal detonator: a grenade projectile (fuse = its lifetime) from the throwing hand; D.nade = the last one
function D.throw(ox, oy, oz, dx, dy, dz, spd, fuse, grp, ent)
  local p = D.p or P()
  local ok, b = pcall(projectile.Launch, 1, V(ox, oy, oz), V(dx, dy, dz), spd, fuse, p, nil, grp, ent)
  if ok and b then D.nade = b return rid(b) end
  return '0'
end
function D.nadepos()
  local b = D.nade
  if not b then return 0 end
  local ok, q = pcall(location.GetPosition, b)
  if ok and q then return 1, q.x, q.y, q.z end
  return 0
end
-- throw preview: where the ballistic path from (x,y,z) at (vx,vy,vz) under gravity g meets the static world, in steps
-- of dt up to tmax: 1, t, x, y, z (0, tmax, last point = it does not land in time)
function D.land(x, y, z, vx, vy, vz, g, dt, tmax)
  local p = D.p or P()
  local px, py, pz, t = x, y, z, 0
  while t < tmax do
    t = t + dt
    local nx, ny, nz = x + vx * t, y + vy * t - 0.5 * g * t * t, z + vz * t
    local ok, r = pcall(physics.RayCast, V(px, py, pz), V(nx, ny, nz), p, 1)
    if ok and r and r.success and r.pos then
      local q = r.pos
      local sx, sy, sz = nx - px, ny - py, nz - pz
      local seg = math.sqrt(sx * sx + sy * sy + sz * sz)
      local f = (seg > 0 and r.dist) and math.min(1, r.dist / seg) or 1
      return 1, t - dt + dt * f, q.x, q.y, q.z
    end
    px, py, pz = nx, ny, nz
  end
  return 0, tmax, px, py, pz
end

-- scan pulse: living AI of another faction within r metres (max n): "x,y,z;x,y,z;..."
function D.scan(r, n)
  local p = D.p or P()
  local ok, pp = pcall(location.GetPosition, p)
  if not ok or not pp then return '' end
  local ok2, all = pcall(scripts.GetAllInRadius, pp, r, n)
  if not ok2 or type(all) ~= 'table' then return '' end
  local okf, pf = pcall(social.GetRenFactionId, p)
  local out = {}
  for _, e in pairs(all) do
    local oa, a = pcall(ai.IsAlive, e)
    if oa and a then
      local of, f = pcall(social.GetRenFactionId, e)
      if not (okf and of and f == pf) then
        local oq, q = pcall(location.GetPosition, e)
        if oq and q then out[#out + 1] = string.format('%.2f,%.2f,%.2f', q.x, q.y, q.z) end
      end
    end
  end
  return table.concat(out, ';')
end

-- bacta stim: heal a fraction of max health (0 = just read); returns the health fraction before
function D.heal(frac)
  local p = D.p or P()
  local ok, m = pcall(game.GetMaxHealth, p)
  local ok2, h = pcall(game.GetHealth, p)
  if not (ok and ok2 and m and h and m > 0) then return -1 end
  if frac > 0 then pcall(game.Heal, p, p, m * frac) end
  return h / m
end

)LUA" R"LUA(
-- jump pack: first = 1: the game's own jump; 0: lift by up metres and drift (dx, dz) x fwd metres, unless the static
-- world is in the way (ceiling above the head, wall ahead); 2: hover, the fall held to up m/s over this frame's dt (the
-- engine's upward velocity takes over once the jump turns into a fall; before that the extra fall is lifted back),
-- 'l' once the feet are on the ground
function D.jet(first, dx, dz, up, fwd, dt)
  local p = D.p or P()
  if first == 1 then pcall(player.Jump, 0) return 'j' end
  local ok, q = pcall(location.GetPosition, p)
  if not ok or not q then return 'x' end
  if first == 2 then
    local okg, rg = pcall(physics.RayCast, V(q.x, q.y + 0.3, q.z), V(q.x, q.y - 0.2, q.z), p, 1)
    if okg and rg and rg.success then return 'l' end
    pcall(player.SetUpwardVelocity, p, -up)
    local okv, v = pcall(physics.GetLinearVelocity, p)
    local vy = okv and v and v.y or 0
    up = (vy < -up and dt and dt > 0) and math.min((-vy - up) * dt, 0.5) or 0
  end
  if up > 0 then
    local okc, rc = pcall(physics.RayCast, V(q.x, q.y + 1.9, q.z), V(q.x, q.y + 2.2 + up, q.z), p, 1)
    if okc and rc and rc.success then up = 0 end
  end
  local mx, mz = dx * fwd, dz * fwd
  if mx ~= 0 or mz ~= 0 then
    local l = math.sqrt(dx * dx + dz * dz)
    local okw, rw = pcall(physics.RayCast, V(q.x, q.y + 1.0, q.z), V(q.x + dx / l * 0.6 + mx, q.y + 1.0, q.z + dz / l * 0.6 + mz), p, 1)
    if okw and rw and rw.success then mx, mz = 0, 0 end
  end
  if up == 0 and mx == 0 and mz == 0 then return 'b' end
  pcall(location.SetPosition, p, V(q.x + mx, q.y + up, q.z + mz))
  return 'm'
end

-- jump pack thrusters: the jump trooper's flame and smoke on two nozzles on the back (spine[3] + the worn body's
-- offset, x = left, mirrored), the first one with the thrust loop sound. on = 1 lights them with an ignition burst,
-- 2 keeps their glow on the surroundings, 0 puts them out. Made once per player ren and reused.
function D.jetfx(on, x, y, z, pitch)
  local p = D.p or P()
  if not p then return 0 end
  local j = D.jets
  if j and (j.p ~= p or not valid(j[1])) then j = nil D.jets = nil end
  if on == 0 then
    if j then for i = 1, #j do pcall(vfx.Stop, j[i]) end end
    return 0
  end
  if on == 1 and (not j or j.x ~= x or j.y ~= y or j.z ~= z or j.pitch ~= pitch) then
    if j then for i = 1, #j do pcall(vfx.Stop, j[i]) pcall(attachment.Detach, p, j[i]) pcall(ren.Destroy, j[i]) end end
    j = { p = p, x = x, y = y, z = z, pitch = pitch }
    for i = 1, 2 do
      local nm = 'DKJet' .. i
      local okp, a = pcall(attachment.AddPoint, p, nm, 'spine[3]', V(i == 1 and x or -x, y, z), 0, pitch, 0)
      local okc, r = pcall(vfx.Create, i == 1 and 'dk_jet.vfx' or 'dk_jet_ns.vfx')
      if okc and r then
        local okb, b = pcall(attachment.AttachToPoint, p, nm, r)
        if okp and a and okb and b then j[#j + 1] = r else pcall(ren.Destroy, r) end
      end
    end
    if #j == 0 then return 0 end
    D.jets = j
  end
  if not j then return 0 end
  if on == 1 then for i = 1, #j do pcall(vfx.Start, j[i]) pcall(vfx.Trigger, 'dk_jet_boost.vfx', j[i]) end end
  pcall(vfx.TriggerVfxLight, p, 0.32, 0.03, on == 1 and 2.4 or 1.3, 1.0, V(1.0, 0.58, 0.22), 0.4, 3.5)
  return #j
end

-- camera ray: hit flag, hit point, distance, 1 if the hit is a living AI
function D.ray(x1, y1, z1, x2, y2, z2)
  local p = D.p or P()
  local ok, r = pcall(physics.RayCast, V(x1, y1, z1), V(x2, y2, z2), p, 0x10800)
  local hit = ok and r and r.success
  if hit and r.ren_id then
    local ok2, a = pcall(ai.IsAlive, r.ren_id)
    if ok2 and a then local q = r.pos return 1, q.x, q.y, q.z, r.dist, 1 end
  end
  local dx, dy, dz = x2 - x1, y2 - y1, z2 - z1
  local l = math.sqrt(dx * dx + dy * dy + dz * dz)
  if l > 0.01 then
    local e, x, y, z = snap(x1, y1, z1, x2, y2, z2, dx / l, dy / l, dz / l, hit and r.dist and (r.dist - 0.05) or l, p)
    if e then
      local gx, gy, gz = x - x1, y - y1, z - z1
      return 1, x, y, z, math.sqrt(gx * gx + gy * gy + gz * gz), 1
    end
  end
  if hit then local q = r.pos return 1, q.x, q.y, q.z, r.dist, 0 end
  return 0
end

rawset(_G, 'DKB', D)
return 'ok'
)LUA";

// the module's version = a hash of its text: any change to the Lua reloads the table on hot reload
static unsigned ModVer() {
    static unsigned h;
    if (!h) {
        h = 2166136261u;
        for (const char* p = kModule; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
        h = (h & 0x00FFFFFFu) | 1u;   // exact in the engine's FLOAT Lua numbers (24-bit mantissa), never 0
    }
    return h;
}
static unsigned s_loadedVer;

bool LuaEnsure() {
    if (!dk::lua::Ready()) return false;
    char out[64];
    if (!dk::lua::Run("local d = rawget(_G, 'DKB') return string.format('%d', d and d.ver or 0)", out, sizeof out, "=DKB")) return false;
    if (strtoul(out, nullptr, 10) == ModVer()) return true;
    char err[256];
    if (!dk::lua::Run(kModule, err, sizeof err, "=DKB") || strcmp(err, "ok")) {
        dk::Log("Lua module load failed: %s", err);
        return false;
    }
    char set[64];
    sprintf_s(set, "DKB.ver = %u return 'ok'", ModVer());
    dk::lua::Run(set, err, sizeof err, "=DKB");
    if (s_loadedVer != ModVer()) dk::Log("Lua module DKB loaded (version %08X)", ModVer());
    s_loadedVer = ModVer();
    return true;
}

static char s_res[1024];
const char* LuaCall(const char* fmt, ...) {
    char code[1024];
    va_list a; va_start(a, fmt); _vsnprintf_s(code, _TRUNCATE, fmt, a); va_end(a);
    if (!LuaEnsure()) { strcpy_s(s_res, "error: no Lua"); return s_res; }
    dk::lua::Run(code, s_res, sizeof s_res, "=DKB");
    return s_res;
}

int LuaNums(float* out, int max, const char* fmt, ...) {
    char code[1024];
    va_list a; va_start(a, fmt); _vsnprintf_s(code, _TRUNCATE, fmt, a); va_end(a);
    if (!LuaEnsure()) return -1;
    return dk::lua::RunNums(code, out, max);
}

}  // namespace dkb
