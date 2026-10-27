# DTREK Modern - Stage 5

A Windows conversion of the 1989 X11/SunOS game **DTREK**, built as a
platform-independent C simulation core with a .NET 10 front-end.

The original source in `C:\dtrek\Dtrek` is a read-only reference and is not
modified by this project. All conversion work lives under `C:\dtrek\modern` and
is published at https://github.com/ceasterd/DTREK-WINDOWS.

---

## What Stage 5 contains

- A native C core (`dtrek_core.dll`) with a stable C ABI.
- A C# wrapper (`Dtrek.Core`) exposing the engine through P/Invoke.
- A console smoke test that exercises movement, phasers, torpedoes, damage,
  subsystem hits and repair, point defense, every ordnance class, galaxy
  generation, orbit services, hazards, robot AI, and RNG.
- A WPF front-end with a vector star field, HUD, keyboard helm, five
  selectable ordnance classes, weapon lock, a generated galaxy, orbit and
  repair controls, a damage-control panel, and a native-core Romulan robot
  named `Warbird`.
- A native C test suite and a PowerShell build script.

### Simulation rules carried over

- 200,000-unit galaxy, reflective boundaries, `WARP1 = 25`.
- Fixed 100 ms updates, 10 updates per second.
- Original 256-direction `Sin`/`Cos` tables and `Ihypot`/`Isqrt`.
- POSIX `lrand48`-compatible deterministic RNG (same sequence as the original).
- Original turn, acceleration, deceleration, warp-cost, and recharge formulas.
- Multi-bank phaser fire with range attenuation and shield facing.
  This build fires the player's phasers along the ship's heading and selects
  the closest hostile in the arc (a torpedo takes priority when it is closer);
  the original let the player click an aim point on the map.
- Five ordnance classes derived from the original weapon prototypes:
  photon, missile, plasma, chaff, and mine. New ships start with a team
  default (Federation/Independent/Orion photons, Romulan plasma, Klingon
  missiles); a training ship may select any class, and all five tubes share
  the selection.
- Photon torpedoes: five tubes, 14 speed, 42 damage, 420 fuel, 20-tick reload,
  a `20 + 0..29` tick fuse, and the original one-in-five wobble.
- Missiles: 8 speed, 21 damage, 210 fuel, 30-tick reload, 50-tick base fuse.
  Missiles enter tracking mode when launched with a live hostile lock and
  otherwise behave like unguided torpedoes. A dead or invalid lock demotes
  the missile to straight flight.
- Plasma: 8 speed, 110 damage, 1,100 fuel, 60-tick reload, fixed 100-tick
  fuse, and `(damage / 10) * 3 / 4` decay every ten ticks, exactly as the
  original plasma prototype. Plasma tracks a locked target.
- Chaff: starts 800 units ahead on the ship's heading and speed, arms after
  ten ticks, deals 19 damage to every ship in blast range, then keeps flying
  straight and can detonate again on later contacts. Phasers never select
  chaff as a point-defense target.
- Mines: stationary, 35 damage, 35 fuel, 60-tick reload, arms after twenty
  ticks. The aim point is clamped to `WPN_MINE_RANGE` (10,000 units); a
  negative aim coordinate places the mine one range ahead along the course.
- Torpedo flight uses boundary loss, proximity fuses, and original linear
  blast falloff. A torpedo detonates within 400 units of a hostile hull and
  damages every live ship within 800 units. Friendly fire, including the
  launching ship, is applied exactly as in `daemon.c`.
- Point defense: phasers can engage hostile torpedoes in the firing arc.
  Photons take double beam damage, plasma half, and missiles/mines normal
  damage. If the beam damage exceeds the remaining warhead damage, the
  torpedo detonates. The original required defensive beam mode plus a manual
  aim point; this build always offers the nearest incoming torpedo and
  spends a single beam on it.
- Damage order: shield -> armor -> hull, ten-tick explosion, kill credit.
- Reliant-class trainer values decoded from `shiparchives/shipdefaults`:
  hull 150 ktons (83 hull points), armor 1, front/right/rear/left shields
  158/92/110/92, 8,500 fuel, 27 engines, 4 phaser banks, 5 torpedo tubes.

### Stage 5 subsystem damage

- Every ship builds the original `shipmenu.c` hit table: armor is unit 0 and
  hull unit 1, followed by engine, fuel, four beam banks, five torpedo tubes,
  dilithium, and cargo. For the Reliant the units are 1 armor, 83 hull,
  27 engine, 17 fuel, four 6-kt beam banks, five 5-kt tubes, 9 dilithium, and
  3 cargo. `system_size`, the pool used for random subsystem hits, is
  105 ktons and excludes armor and hull.
- A hit strips shields first, then armor, then hull. After the hull has taken its
  share, the original spills the whole post-armor figure over the remaining
  systems; it never subtracts the hull's share from that spill. A 72-point
  volley against a 1-armor Reliant therefore takes the armor, 71 hull points,
  and still damages other systems. This quirk is preserved deliberately.
- A subsystem other than armor or hull loses a random slice of its remaining
  size, capped by `DAMAGE_SPLITUP` (10 ktons). Losing parts changes the ship:
  engine condition scales `engine_health` and therefore drive performance;
  fuel damage removes 500 fuel capacity per kton; a destroyed beam bank or
  torpedo tube leaves the ship (plasma ships keep the tube but lose its
  condition); dilithium damage lowers recharge; cargo damage removes cargo.
  Armor and hull units stay synchronized with the existing ship fields.
- Self repair uses the original `REPAIRFUSE = 30` cadence: the first pulse
  lands on tick 30, and every damaged repairable unit gains its rate in
  1/256-kt units per pulse. Rates below are raw 1/256 units per pulse; the
  simulation carries the fraction and applies `rate >> 8` points at a time.

  | Unit | Self | Extra at a base |
  | --- | ---: | ---: |
  | Armor | 0 | 1,500 |
  | Hull | 562 | 3,000 |
  | Engine | 140 | 450 |
  | Fuel | 112 | 450 |
  | Beam bank | 168 | 540 |
  | Torpedo tube | 84 | 270 |
  | Dilithium | 39 | 150 |
  | Cargo | 168 | 450 |

- Damage control (`repair_mode`) doubles self repair while the ship is
  stationary, even away from a base. Orbiting a friendly repair world adds the
  base rate. Armor has no self rate, so it can only be restored at a base, and
  its 1-point maximum means one pulse restores at most that single point.
  A friendly base still rearms every functional bank and tube each tick.

### Stage 3 places

- The authentic 80-object galaxy from `planets.c`: four team home worlds at
  `44444/155556`, `44444/44444`, `155556/44444`, and `155556/155556`; the
  independent home at the galactic center; nine member worlds per team; a
  wandering independent; and black holes, pulsars, nebulae, barren worlds, and
  independents distributed per quadrant. Home worlds are always fuel/repair
  bases; other worlds roll their services from the original probabilities.
- Galaxy generation uses its own deterministic generator. It never advances
  the combat RNG, so an existing seed still produces the same combat sequence.
  Engines start with an empty map; the galaxy is generated explicitly.
- Planets own armies, a team mask, and feature bits. Independents are friendly
  to every team, and a planet's own team is friendly to itself.
- Orbit entry requires warp speed at or below 2 and range at or below 900
  units. The ship snaps to 800 units and rotates two units per tick; black
  holes and nebulae cannot be orbited.
- A friendly fuel planet adds `recharge * 2 + 6` fuel per tick while in orbit
  instead of `recharge * 2`.
- Damage control (`T`) doubles subsystem self repair while the ship is
  stationary and, in orbit of a friendly repair base, adds the planet repair
  rates, restores armor, regenerates shields four times as fast, and rearms
  functional phaser banks and torpedo tubes.
- Black holes use the original gravity formula and destroy a ship that comes
  within 1.5 orbit radii. Pulsars destroy ships inside `ORBDIST + 10`.
- Torpedoes are removed by planetary impact inside `ORBDIST^2 / 2`; nebulae
  do not stop torpedoes, and black holes deflect torpedo courses.
- Wanderers drift every tick and choose a new course with probability
  `1 / 1200`, bouncing at the galaxy edges.

### Stage 4 robots

- The original `rmove.c` decision loop is ported into `native/src/robot.c` and
  runs inside `dtrek_step`, before ship movement. Robots are ordinary Reliant
  slots with an `is_robot` flag, so snapshots and combat use the existing ABI.
- Each robot selects a target with last-target hysteresis, then follows the
  original order: fire, evade incoming torpedoes, run away when badly hurt,
  repair or seek a repair base, attack or patrol, avoid planets and edges, and
  handle attack orbits. The closest-torpedo projection bug from the 1989 code
  is fixed so evasion tracks the true minimum time to impact.
- Defaults come from `robot.c`: offense `70 + 10 * rand() % 3` (100 for
  orbiters, clamped 10..100), impatience `20 + rand() % 80` (clamped 50..100,
  then adjusted for plasma, missile, and independent ships), and team attack
  speeds: independent 2, Federation 7, Romulan 5, Klingon 4, Orion 9.
- `DtrekRobotConfig` can override offense, impatience, attack speed, role flags
  (`ORBITER`, `STARBASE`), and a fixed patrol planet. Zero values keep the
  automatic defaults. `dtrek_spawn_robot`, `dtrek_set_robot`, and
  `dtrek_clear_robot` spawn, retune, and remove robots; clearing also removes
  the robot's torpedoes.
- Robots use a third deterministic stream, `robot_rng`, seeded from the engine
  seed. Robot decisions never advance the combat or galaxy generators, and the
  same seed reproduces the same simulation.
- Robot snapshots expose role, state, target, patrol planet and fuse, evasion
  fuse, and the tick/target/hit of the latest phaser volley and torpedo launch.
  The WPF front-end uses those fields to draw beams and log events without
  running any AI of its own.

### Deliberately not in Stage 5

- Cloak, ECM, tractor beams, hyperspace, and sphere/AFC systems.
- Ship archive import/export, scores, `.planets` state, and XBM bitmaps.
- Teams, treaties, visibility rules, conquest, and networking.

The core keeps the original torpedo slot layout, lifecycle, and damage table,
so robots and the remaining systems can be added without changing the ordnance
or damage ABI.

---

## Prerequisites

- Windows 10 or later, x64.
- Visual Studio 2022 or later with the **Desktop development with C++**
  workload (MSVC and the bundled CMake). The build script selects the newest
  installed Visual Studio generator, so VS 2022 and VS 2026 both work.
- .NET SDK 10.0 or later.
- Inno Setup 6 for `-Package` builds. The installed game is self-contained and
  needs neither the .NET runtime nor the Visual C++ redistributable.

The build script finds CMake and `dotnet` even when they are not on `PATH`. It
also looks for Inno Setup in its per-user and machine-wide default locations.

---

## Build and run

From `C:\dtrek\modern`:

```powershell
# Build everything, then run the native and managed tests
.\scripts\build.ps1 -Test

# Build, test, and create dist\DTREK-0.6.0-win-x64-setup.exe plus a portable ZIP
.\scripts\build.ps1 -Test -Package

# Build and launch the WPF app
.\scripts\build.ps1 -Run

# Debug build
.\scripts\build.ps1 -Configuration Debug

# Rebuild managed projects only (native DLL already present)
.\scripts\build.ps1 -SkipNative
```

Outputs:

```
src\Dtrek.Console\bin\Release\net10.0\Dtrek.Console.exe
src\Dtrek.App\bin\Release\net10.0-windows\Dtrek.App.exe
artifacts\native\dtrek_core.dll
artifacts\native\dtrek_core_tests.exe
dist\DTREK-0.6.0-win-x64\                 (packaged app files)
dist\DTREK-0.6.0-win-x64.zip              (portable build)
dist\DTREK-0.6.0-win-x64-setup.exe        (Inno Setup installer)
```

### Manual build

```powershell
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release --parallel
ctest --test-dir native/build -C Release --output-on-failure

dotnet build Dtrek.sln -c Release
dotnet run --project src/Dtrek.Console -c Release
dotnet run --project src/Dtrek.App -c Release
```

`dtrek_core.dll` is copied beside every managed executable by
`Directory.Build.props`, so both `dotnet run` and direct launches work.

### Packaging

`-Package` runs `dotnet publish` with the `win-x64` profile as a self-contained
folder, verifies that `dtrek_core.dll` is present, creates a portable ZIP, and
compiles `installer/dtrek.iss` with Inno Setup. The native core links the MSVC
runtime statically, so there is no VC++ redistributable prerequisite.

The installer is per-user by default and installs under
`%LOCALAPPDATA%\Programs\DTREK`; the setup dialog also allows an all-users
install. It shows and installs the original DTREK notice from `LICENSE.txt`,
creates Start Menu entries, an optional desktop shortcut, and an uninstaller.
The version comes from `DtrekVersion` in `Directory.Build.props`, which also
stamps the managed assemblies.

### GitHub releases

`.github/workflows/ci.yml` builds and tests pushes and pull requests against
`main`. `.github/workflows/release.yml` runs on tags matching `v*`: it verifies
that the tag matches `DtrekVersion`, builds and tests the project, uploads the
ZIP and installer as workflow artifacts, and attaches them to the GitHub
release. Manual runs build and upload artifacts without creating a release.

---

## Controls

| Key | Action |
| --- | --- |
| `A` / `Left` | Turn left |
| `D` / `Right` | Turn right |
| `W` / `Up` | Increase warp (0..6 for the Reliant) |
| `S` / `Down` | Decrease warp |
| `Space` | Fire all loaded phaser banks along the current heading |
| `F` | Launch the selected ordnance along the current heading |
| `1` - `5` | Select photon, missile, plasma, chaff, or mine |
| `L` | Toggle the weapon lock used by missiles and plasma |
| `O` | Enter orbit around the nearest eligible planet, or leave orbit |
| `T` | Toggle damage control (self repair, base repair at a friendly world) |
| `Tab` | Raise/lower screens |
| `P` | Pause/resume |
| `R` | Restart the patrol |

The green ring is phaser range (7,500 units). The dashed line is the current
heading. The camera follows the player; the galaxy is 200,000 units wide.
Planets are drawn in team colors; a magenta ring marks a fuel depot, a gold
ring marks a repair base, and a dashed green ring shows the current orbit.
Black holes are dark rings, pulsars are white crosses, and nebulae are large
translucent clouds.

Photon torpedoes are unguided: they fly straight along the launch heading so
the helm must be pointed at the target. Missiles and plasma home on a locked
target (`L`); without a lock they fly straight like photons. Mines are placed
one mine range (10,000 units) ahead of the ship, and chaff drifts forward from
the ship and damages anything it passes. Tubes reload one at a time, and the
HUD shows the selected class, its damage and speed, and how many tubes are
loaded. Phasers act as point defense, automatically engaging a hostile
torpedo in the arc when it is closer than the nearest ship.

At low warp the ship's recharge rate exceeds fuel use, exactly as in the
original. Sustained high warp drains fuel.

Helm response follows the original `changedir` formula. A stopped ship turns
instantly, a warp-1 ship reverses within a few updates, and even at warp 6 a
90-degree turn takes about five seconds. The Reliant's maneuverability field is
`89991`, derived by `shipmenu` from its 150-kt hull and 27 engines; the `70%`
shown in the original builder UI is a display value, not the helm field.

Press `O` within 900 units of a planet at warp 2 or less to orbit it; press
`O` again to leave. While orbiting a friendly fuel world the ship recharges
faster. Damage control (`T`) doubles subsystem self repair while the ship is
stationary; at a friendly repair world it also adds the planet repair rate,
repairs armor, and rearms every functional weapon. The navigation HUD shows the
current orbit, the nearest planet and its range, and active services, while the
damage-control panel lists damaged and destroyed systems.

---

## Layout

```
C:\dtrek
  Dtrek\                         original 1989 X11 source (untouched)
  modern\
    Dtrek.sln
    Directory.Build.props        shared managed settings + native DLL copy
    LICENSE.txt                  original DTREK notice and permission
    native\
      include\dtrek_core.h       public C ABI
      src\core.c                 movement, combat, damage, places, snapshots
      src\robot.c                ported rmove() robot decision loop
      src\math.c                 original trig tables, atan2, isqrt, ihypot
      src\rng.c                  lrand48-compatible generator
      src\dtrek_internal.h       private ship/planet/engine structures
      tests\test_core.c          native behavioral tests
      CMakeLists.txt
    src\
      Dtrek.Core\                P/Invoke, DtrekEngine, snapshot/result types
      Dtrek.Console\             interop smoke test
      Dtrek.App\                 WPF front-end (GalaxyView, GameSession, HUD)
        Properties\PublishProfiles\win-x64.pubxml
    installer\dtrek.iss          Inno Setup installer definition
    .github\workflows\           CI and tagged-release pipelines
    scripts\build.ps1
    artifacts\native\            generated DLL, import library, test exe
    dist\                        generated publish, ZIP, and installer (ignored)
```

---

## Architecture

### Native core

`dtrek_core.dll` owns the authoritative simulation. It exposes an opaque
`dtrek_engine *` plus snapshots and commands through `extern "C"`-safe
functions:

```c
dtrek_create / dtrek_destroy / dtrek_reset
dtrek_spawn / dtrek_set_controls / dtrek_set_shields
dtrek_step / dtrek_fire_phaser / dtrek_fire_torpedo / dtrek_snapshot
dtrek_set_ordnance / dtrek_ordnance_spec
dtrek_generate_galaxy / dtrek_clear_planets / dtrek_place_planet
dtrek_orbit / dtrek_leave_orbit / dtrek_set_repair
dtrek_spawn_robot / dtrek_set_robot / dtrek_clear_robot
dtrek_random_below / dtrek_abi_version / dtrek_version_string
```

All snapshot fields are 32-bit integers or fixed `char` arrays, so the layout
matches MSVC, GCC, and C# sequential structs without packing attributes.
Torpedoes use the original per-owner slot layout: owner slot * 24 + index,
for 768 slots total. `dtrek_fire_torpedo` takes a `DtrekTorpedoLaunch`
request (course, lock slot, mine aim point, and optional class override), and
`DtrekOrdnanceSpec` carries the nine profile words plus a 32-byte name.
Planets use the original fixed 80-slot layout, and the snapshot carries each
planet's flags, owner, armies, position, drift, and name.

Each ship snapshot now also carries `system_count`, `system_size`, and up to
`DTREK_MAX_SYSTEMS` (50) `DtrekSystemSnapshot` entries. Each entry reports the
`DtrekSystemItem` and bank/tube number, current and maximum size in ktons, the
1/256-unit repair fraction, and self and planet repair rates. The same
`DtrekSystemItem` enum mirrors the original `system.h` numbering, including
`DTREK_SYS_TORP_NUM` and the armor/hull/fuel/side/rear shield items.

The engine runs at a fixed 100 ms tick. Each tick increments the tick counter,
then moves wanderers, runs each robot's decision step, updates ships (orbit,
movement, reloads, services), then torpedoes. `dtrek_step` advances whole
ticks, so the front-end owns wall-clock pacing and the simulation remains
deterministic and testable. ABI 6 adds the subsystem hit table and repair
telemetry; the version string is `dtrek_core 0.6.0`.

### Managed wrapper

`Dtrek.Core` provides:

- `DtrekEngine` with lifecycle, controls, ordnance selection, stepping,
  phaser fire, torpedo launch, galaxy generation, planet placement, orbit and
  repair control, robot spawn/tune/clear, snapshots, RNG, disposal, and native
  error-to-exception mapping.
- Immutable `GameSnapshot`, `ShipSnapshot`, `ShipSystemSnapshot`,
  `TorpedoSnapshot`, `PlanetSnapshot`, `OrdnanceSpec`, `TorpedoLaunch`,
  `DtrekRobotConfig`, `PhaserResult`, and `TorpedoResult` types.
- `ShipSnapshot.SystemCount`, `SystemSize`, and `Systems` expose the hit table,
  with `DamagedSystemCount`, `HasSystemDamage`, and `FindSystem(item, subitem)`
  helpers. `DtrekSystemItem` mirrors the native enum and
  `DtrekConstants.RepairFuse` is 30.
- `DtrekException` carrying the native `DtrekResult`.
- A native library resolver that loads `dtrek_core.dll` beside the app.

The P/Invoke layer checks the ABI version on engine creation and fails loudly
if managed and native binaries are out of sync. A refused torpedo launch
(reloading tubes or insufficient fuel) returns a result with `Fired == false`
instead of throwing.

### Front-end

`Dtrek.App` is a WPF window on `net10.0-windows`:

- `GameSession` generates the galaxy, applies player input, steps the core,
  handles orbit and damage-control orders, tracks beam effects, detonations,
  interceptions, subsystem damage, and the event log. The hostile `Warbird` is
  a core robot; the session reads its telemetry to present beams, launches, and
  log lines.
- `GalaxyView` draws the star field, galaxy grid, planets and hazards, orbit
  ring, range ring, ships, beams, class-specific ordnance markers (photon
  dots, missile darts, plasma orbs, chaff clusters, mine crosses), tracking
  lines, detonation rings, ship explosions, and mission overlays on a camera
  centered on the player.
- The HUD shows hull, armor, fuel, all four shield facings, helm orders,
  phaser and torpedo reload state, the selected ordnance profile, weapon lock,
  orbit and nearest-planet data, fuel/repair services, torpedoes in flight,
  incoming torpedo warnings, target data, the robot's current behavior and
  aggression, a damage-control panel listing damaged or destroyed units, and
  recent events.

The enemy AI now lives entirely in the native core. The WPF layer no longer
chooses courses or fires enemy weapons; it only renders what `dtrek_step`
reports.

The patrol opens gently: `Warbird` starts 24,000 units east of the player with
a relaxed `DtrekRobotConfig` (offense 50, impatience 50, attack speed 3), which
keeps it outside torpedo range while the player gets underway. To make the
opening harder, move `EnemyStartX`/`EnemyStartY` closer or raise those values in
`GameSession`.

---

## Testing

```powershell
.\scripts\build.ps1 -Test
```

- `dtrek_core_tests.exe` runs 865 native checks covering metadata, spawning,
  deterministic RNG, movement, course changes, shield facing, phaser reload,
  damage ordering, subsystem hit tables, damage spill, degradation, repair
  cadence, explosions, kill credit, torpedo launch/reload, blast falloff and
  friendly fire, point defense, torpedo expiry, boundaries, ordnance profiles,
  missile tracking and demotion, plasma decay and reload, chaff, mines,
  class-specific interception, launch validation, reset, galaxy generation and
  determinism, wanderers, planet placement, orbit entry/leave and validation,
  fuel/repair/rearm services, black holes, pulsars, planetary torpedo impact,
  robot metadata and defaults, robot targeting and combat, torpedo evasion,
  hazard avoidance, repair, patrol, clearing, and robot determinism.
- `Dtrek.Console` runs 266 interop checks with real values from the native DLL
  and prints the movement, volley, torpedo, blast, ordnance, guidance, chaff,
  mine, interception, subsystem damage and repair, galaxy, orbit, service,
  hazard, and robot sequence.

CTest runs the native suite under `native/build`.

---

## Next stages

1. **Ship archives**: import `shiparchives/shipdefaults` and player `.ship`
   entries with explicit versioning; replace the hard-coded trainer spec.
2. **Galaxy state**: scores, `.planets`, team treaties/visibility, conquest,
   save/load.
3. **Fleet robots**: full patrol wings, starbase robots, and team-level
   command behavior beyond the single trainer robot.
4. **Presentation**: original XBM bitmaps and damage-window art, sound, and a
   proper ship-selection flow.

---

## Copyright and permission

This is a derivative conversion of DTREK. The original notice is preserved in
the new source files:

Copyright (c) 1986 Chris Guthrie. Major overhaul Copyright (c) 1989 Daryl Poe.
Ported to X11 by Jim Andreas. Tractors, treaties, and other features by Norm
Gee. Damage window and shield bitmaps by Tom LaStrange.

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted, provided that
the above copyright notice appear in all copies and that both that copyright
notice and this permission notice appear in supporting documentation. No
representations are made about the suitability of this software for any
purpose. It is provided "as is" without express or implied warranty.
