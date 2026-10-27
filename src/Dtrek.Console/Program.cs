using Dtrek.Core;

var checks = 0;
var failures = 0;

void Check(bool condition, string message)
{
    checks++;
    if (condition)
    {
        Console.WriteLine($"  PASS  {message}");
        return;
    }

    failures++;
    Console.WriteLine($"  FAIL  {message}");
}

static int SumShields(ShipSnapshot ship)
    => ship.Shields.Sum();

static string SnapshotDigest(GameSnapshot snapshot)
{
    var ships = string.Join(";", snapshot.Ships
        .Where(ship => ship.InUse)
        .Select(ship => string.Join(",",
            ship.Slot,
            ship.Team,
            ship.Status,
            ship.X,
            ship.Y,
            ship.Course,
            ship.Speed,
            ship.DesiredCourse,
            ship.DesiredSpeed,
            ship.Hull,
            ship.Fuel,
            ship.BeamsLoaded,
            ship.TubesLoaded,
            ship.RobotState,
            ship.RobotTargetSlot,
            ship.RobotPatrolFuse,
            ship.RobotEvasionFuse,
            ship.RobotLastBeamTick,
            ship.RobotLastTorpedoTick)));

    var torpedoes = string.Join(";", snapshot.Torpedoes.Select(torpedo =>
        string.Join(",", torpedo.Slot, torpedo.OwnerSlot, torpedo.Status,
            torpedo.X, torpedo.Y, torpedo.Course, torpedo.Speed,
            torpedo.Fuse, torpedo.Damage)));

    return $"{snapshot.Tick}|{snapshot.RngState}|{ships}|{torpedoes}";
}

static string RobotScenarioDigest(uint seed)
{
    using var engine = new DtrekEngine(seed);
    engine.PlacePlanet(
        0,
        DtrekPlanetFlags.Fuel | DtrekPlanetFlags.Repair,
        DtrekTeam.Federation,
        armies: 10,
        x: 100000,
        y: 100000,
        name: "Earth");
    engine.PlacePlanet(
        1,
        DtrekPlanetFlags.Repair,
        DtrekTeam.Romulan,
        armies: 10,
        x: 44444,
        y: 44444,
        name: "Chulak");
    engine.Spawn("Kirk", DtrekTeam.Federation, 100000, 102000);
    engine.Spawn("Targ", DtrekTeam.Romulan, 105000, 99000);
    engine.SpawnRobot(
        "Warbird",
        DtrekTeam.Romulan,
        104000,
        98000,
        new DtrekRobotConfig
        {
            Offense = 80,
            Impatience = 80,
            AttackSpeed = 5,
        });

    engine.Step(150);
    return SnapshotDigest(engine.CaptureSnapshot());
}

static int BlastDamage(int fullDamage, int dx, int dy)
{
    const int explode = DtrekConstants.TorpedoExplodeDistance;
    const int detonate = DtrekConstants.TorpedoDetonateDistance;
    var distanceSquared = dx * dx + dy * dy;

    if (distanceSquared > detonate * detonate)
    {
        return 0;
    }

    if (distanceSquared > explode * explode)
    {
        var distance = (int)Math.Sqrt(distanceSquared);
        return fullDamage * (detonate - distance) / (detonate - explode);
    }

    return fullDamage;
}

Console.WriteLine("=== DTREK modern core - Stage 5 smoke test ===");
Console.WriteLine();

try
{
    Console.WriteLine("[native library]");
    Console.WriteLine($"  ABI version : {DtrekEngine.NativeAbiVersion}");
    Console.WriteLine($"  build       : {DtrekEngine.NativeVersion}");
    Check(DtrekEngine.NativeAbiVersion == DtrekEngine.SupportedAbiVersion,
        "managed wrapper and native ABI agree");
    Console.WriteLine();

    // ------------------------------------------------------------------
    // Movement and deterministic randomness.
    // ------------------------------------------------------------------
    Console.WriteLine("[movement]");
    using (var engine = new DtrekEngine(seed: 1989))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var start = engine.CaptureSnapshot();
        var startShip = start.GetShip(player);

        Console.WriteLine(
            $"  spawn       : slot {player}, {startShip.Name}, "
            + $"hull {startShip.Hull}, armor {startShip.Armor}, "
            + $"fuel {startShip.Fuel}");
        Console.WriteLine(
            $"  position    : ({startShip.X}, {startShip.Y})  "
            + $"course {startShip.Course,3}  speed {startShip.Speed}");

        engine.SetControls(player, desiredCourse: 64,
            desiredSpeed: DtrekConstants.MaxSpeed);
        engine.Step(300);

        var after = engine.CaptureSnapshot();
        var afterShip = after.GetShip(player);
        var moved = (int)Math.Round(Math.Sqrt(
            Math.Pow(afterShip.X - startShip.X, 2)
            + Math.Pow(afterShip.Y - startShip.Y, 2)));

        Console.WriteLine(
            $"  after 300   : ({afterShip.X}, {afterShip.Y})  "
            + $"course {afterShip.Course,3}  speed {afterShip.Speed}  "
            + $"fuel {afterShip.Fuel}");
        Console.WriteLine($"  moved       : {moved} units in 30 s");
        Console.WriteLine(
            "  note        : at low warp the reloader outpaces fuel use, "
            + "exactly as the original");

        Check(after.Tick == 300, "simulation advanced 300 ticks");
        Check(afterShip.X > startShip.X, "ship moved east along course 64");
        Check(afterShip.Speed > 0, "ship accelerated");
        Check(afterShip.Fuel < startShip.Fuel, "movement consumed fuel");
        Check(afterShip.Speed <= DtrekConstants.MaxSpeed,
            "speed stayed within the original limit");

        // Steering must stay usable at high warp.  The port once used the
        // shipmenu display percentage instead of the archived helm field.
        engine.SetControls(player, desiredCourse: 128,
            desiredSpeed: DtrekConstants.MaxSpeed);
        var beforeTurn = engine.CaptureSnapshot().GetShip(player);
        engine.Step(20);
        var turning = engine.CaptureSnapshot().GetShip(player);
        Console.WriteLine(
            $"  warp turn   : course {beforeTurn.Course,3} -> "
            + $"{turning.Course,3} at warp {turning.Speed} after 2 s");
        Check(beforeTurn.Speed == 6,
            "warp turn begins at the Reliant's maximum warp");
        Check(turning.Course > beforeTurn.Course && turning.Course < 128,
            "ship turns toward the helm while at maximum warp");
    }

    Console.WriteLine();
    Console.WriteLine("[deterministic rng]");
    using (var a = new DtrekEngine(seed: 12345))
    using (var b = new DtrekEngine(seed: 12345))
    {
        var first = Enumerable.Range(0, 6).Select(_ => a.NextRandom(1000)).ToArray();
        var second = Enumerable.Range(0, 6).Select(_ => b.NextRandom(1000)).ToArray();

        Console.WriteLine($"  seed 12345  : [{string.Join(", ", first)}]");
        Console.WriteLine($"  repeat      : [{string.Join(", ", second)}]");
        Check(first.SequenceEqual(second),
            "two engines produce the same lrand48 sequence");
        Check(first.All(v => v is >= 0 and < 1000), "values stay in range");
    }

    // ------------------------------------------------------------------
    // Phaser combat and damage.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[phaser combat]");
    using (var engine = new DtrekEngine(seed: 1989))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 103000, 100000);

        var before = engine.CaptureSnapshot();
        var kirk = before.GetShip(player);
        var targ = before.GetShip(enemy);

        Console.WriteLine(
            $"  Kirk        : slot {player}, {kirk.Name}, team {kirk.Team}, "
            + $"beams {kirk.BeamsLoaded}/{kirk.BeamsTotal}");
        Console.WriteLine(
            $"  Targ        : slot {enemy}, {targ.Name}, team {targ.Team}, "
            + $"rear shield {targ.RearShield}, hull {targ.Hull}");

        var shot = engine.FirePhaser(player, aimCourse: 64);
        Console.WriteLine(
            $"  volley      : fired={shot.Fired} hit={shot.Hit} "
            + $"range={shot.Range} damage={shot.Damage} "
            + $"shield={shot.Shield} absorbed={shot.ShieldAbsorbed} "
            + $"fuel={shot.FuelCost}");

        var hit = engine.CaptureSnapshot();
        var hitTarg = hit.GetShip(enemy);
        var hitKirk = hit.GetShip(player);

        Check(shot.Fired && shot.Hit, "phaser volley hit the Romulan");
        Check(shot.TargetSlot == enemy, "volley selected the hostile ship");
        Check(shot.BeamsFired == 4, "all four phaser banks fired");
        Check(shot.Damage == 72, "range-3000 volley deals 72 damage");
        Check(shot.Shield == DtrekShield.Rear, "incoming fire hit rear shield");
        Check(hitTarg.RearShield == targ.RearShield - shot.Damage,
            "damage was removed from the rear shield");
        Check(hitKirk.BeamsLoaded == 0, "banks are empty while reloading");
        Check(hitKirk.Fuel == kirk.Fuel - shot.FuelCost,
            "volley consumed the expected fuel");

        try
        {
            engine.FirePhaser(player, aimCourse: 64);
            Check(false, "firing again during reload is rejected");
        }
        catch (DtrekException ex) when (ex.Result == DtrekResult.WeaponNotReady)
        {
            Check(true, "firing again during reload is rejected");
        }

        Console.WriteLine("  reload      : waiting 120 ticks (12 s)");
        engine.Step(120);
        var reloaded = engine.CaptureSnapshot();
        Check(reloaded.GetShip(player).BeamsLoaded == 4,
            "all banks reloaded after the original reload time");

        // Drop the target's shields to demonstrate armor and hull damage.
        engine.SetShields(enemy, up: false);
        var unshielded = engine.FirePhaser(player, aimCourse: 64);
        Console.WriteLine(
            $"  unshielded  : damage={unshielded.Damage} "
            + $"armor={unshielded.ArmorAbsorbed} "
            + $"hull={unshielded.HullDamage} "
            + $"destroyed={unshielded.TargetDestroyed}");

        var damaged = engine.CaptureSnapshot();
        Check(unshielded.ShieldAbsorbed == 0,
            "lowered shields absorbed nothing");
        Check(unshielded.ArmorAbsorbed == 1,
            "the single armor point absorbed one damage");
        Check(unshielded.HullDamage == 71,
            "the hull took the remaining damage");
        Check(damaged.GetShip(enemy).Hull == targ.Hull - 71,
            "hull points match the reported damage");

        engine.Step(120);
        var killing = engine.FirePhaser(player, aimCourse: 64);
        Console.WriteLine(
            $"  killing blow: damage={killing.Damage} "
            + $"destroyed={killing.TargetDestroyed}");

        var exploding = engine.CaptureSnapshot();
        Console.WriteLine(
            $"  Targ status : {exploding.GetShip(enemy).Status}, "
            + $"Kirk kills {exploding.GetShip(player).Kills}");
        Check(killing.TargetDestroyed, "third volley destroyed the target");
        Check(exploding.GetShip(enemy).Status == DtrekPlayerStatus.Exploding,
            "destroyed ship enters the explosion state");
        Check(exploding.GetShip(player).Kills == 1, "kill was credited");
        Check(exploding.ActiveCount == 1, "only one ship remains active");

        engine.Step(10);
        var dead = engine.CaptureSnapshot();
        Check(dead.GetShip(enemy).Status == DtrekPlayerStatus.Dead,
            "explosion resolved after ten ticks");
    }

    // ------------------------------------------------------------------
    // Photon torpedoes.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[torpedo launch]");
    using (var engine = new DtrekEngine(seed: 1989))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var before = engine.CaptureSnapshot();
        var kirk = before.GetShip(player);

        Console.WriteLine(
            $"  tubes       : {kirk.TubesLoaded}/{kirk.TubesTotal}  "
            + $"fuel {kirk.Fuel}");

        var shot = engine.FireTorpedo(player, course: 64);
        Console.WriteLine(
            $"  launch      : fired={shot.Fired} slot={shot.TorpedoSlot} "
            + $"class={shot.WeaponClass} damage={shot.Damage} "
            + $"speed={shot.Speed} fuse={shot.Fuse} fuel={shot.FuelCost}");

        var after = engine.CaptureSnapshot();
        var afterKirk = after.GetShip(player);
        var torpedo = after.Torpedoes.Single();

        Check(shot.Fired, "torpedo left the tube");
        Check(shot.WeaponClass == DtrekWeaponClass.Photon,
            "torpedo is a photon");
        Check(shot.Damage == 42 && shot.Speed == 14,
            "torpedo carries the Reliant warhead");
        Check(shot.Fuse is >= 20 and <= 49,
            "fuse is duration plus up to 29 random ticks");
        Check(shot.TubesLoaded == 4 && shot.TubesTotal == 5,
            "one of five tubes was emptied");
        Check(after.TorpedoCount == 1, "snapshot reports one torpedo");
        Check(torpedo.X == 100000 && torpedo.Y == 100000,
            "torpedo starts at the ship");
        Check(torpedo.Course == 64 && torpedo.Status == TorpedoStatus.Move,
            "torpedo is moving east");
        Check(torpedo.Team == DtrekTeam.Federation,
            "torpedo carries its owner's team");
        Check(torpedo.TargetSlot == -1, "photon torpedo is unguided");
        Check(torpedo.IsInFlight, "torpedo is in flight");
        Check(afterKirk.TubesReload == 20, "tube reload started");
        Check(afterKirk.TorpedoesInFlight == 1, "one torpedo in flight");
        Check(afterKirk.Fuel == kirk.Fuel - 420,
            "launch consumed torpedo fuel");

        for (var i = 0; i < 4; i++)
        {
            var rapid = engine.FireTorpedo(player, course: 64);
            Check(rapid.Fired, $"rapid launch {i + 2} accepted");
            Check(rapid.TubesLoaded == 3 - i,
                $"tube count drops to {3 - i} after launch {i + 2}");
        }

        var refused = engine.FireTorpedo(player, course: 64);
        Check(!refused.Fired,
            "sixth launch is refused while the tubes reload");

        var empty = engine.CaptureSnapshot();
        Check(empty.GetShip(player).TubesLoaded == 0,
            "all five tubes are empty");
        Check(empty.TorpedoCount == 5, "five torpedoes are in flight");
        Check(empty.GetShip(player).Fuel == kirk.Fuel - 5 * 420,
            "five launches consumed five times the cost");

        engine.Step(19);
        Check(engine.CaptureSnapshot().GetShip(player).TubesLoaded == 0,
            "reload takes the full twenty ticks");

        engine.Step(1);
        Check(engine.CaptureSnapshot().GetShip(player).TubesLoaded == 1,
            "the first tube finished reloading");

        engine.Step(80);
        var reloaded = engine.CaptureSnapshot();
        Check(reloaded.GetShip(player).TubesLoaded == 5, "all tubes reloaded");
        Check(reloaded.TorpedoCount == 0, "expired torpedoes left the slots");
    }

    Console.WriteLine();
    Console.WriteLine("[torpedo blast]");
    using (var engine = new DtrekEngine(seed: 2024))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var target = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 100200, 100000);
        var bystander = engine.Spawn(
            "Bystander", DtrekTeam.Romulan, 100350, 100500);

        var before = engine.CaptureSnapshot();
        engine.FireTorpedo(player, course: 64);
        engine.Step(1);

        var after = engine.CaptureSnapshot();
        var torpedo = after.Torpedoes.Single(t => t.IsDetonating);
        var kirk = before.GetShip(player);
        var targ = before.GetShip(target);
        var other = before.GetShip(bystander);

        var toKirk = BlastDamage(
            42, torpedo.X - kirk.X, torpedo.Y - kirk.Y);
        var toTarget = BlastDamage(
            42, torpedo.X - targ.X, torpedo.Y - targ.Y);
        var toBystander = BlastDamage(
            42, torpedo.X - other.X, torpedo.Y - other.Y);

        Console.WriteLine(
            $"  detonation  : ({torpedo.X}, {torpedo.Y})  "
            + $"fuse {torpedo.Fuse}");
        Console.WriteLine(
            $"  damage      : owner {toKirk}, target {toTarget}, "
            + $"bystander {toBystander}");

        Check(torpedo.IsDetonating, "proximity fuse detonated the torpedo");
        Check(torpedo.Fuse == 10, "detonation lasts ten ticks");
        Check(toKirk == 42 && toTarget == 42,
            "ships inside 400 take full damage");
        Check(toBystander is > 0 and < 42,
            "ship in the 400..800 band takes falloff damage");
        Check(SumShields(after.GetShip(player)) == SumShields(kirk) - toKirk,
            "the launching ship takes its own blast");
        Check(SumShields(after.GetShip(target)) == SumShields(targ) - toTarget,
            "the target took full blast damage");
        Check(SumShields(after.GetShip(bystander))
                == SumShields(other) - toBystander,
            "the bystander took falloff damage");

        engine.Step(11);
        Check(engine.CaptureSnapshot().TorpedoCount == 0,
            "detonation cleared after ten ticks");
    }

    Console.WriteLine();
    Console.WriteLine("[torpedo point defense]");
    using (var engine = new DtrekEngine(seed: 4242))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 103000, 100000);

        engine.SetOrdnance(enemy, DtrekWeaponClass.Photon);

        var launch = engine.FireTorpedo(enemy, course: 192);
        Check(launch.Fired, "enemy launched an incoming torpedo");
        engine.Step(5);

        var before = engine.CaptureSnapshot();
        var incoming = before.Torpedoes.Single(t => t.OwnerSlot == enemy);
        var kirk = before.GetShip(player);
        var range = (int)Math.Round(Math.Sqrt(
            Math.Pow(incoming.X - kirk.X, 2)
            + Math.Pow(incoming.Y - kirk.Y, 2)));

        Console.WriteLine(
            $"  incoming    : slot {incoming.Slot} "
            + $"at ({incoming.X}, {incoming.Y}) "
            + $"range {range} damage {incoming.Damage}");

        var shot = engine.FirePhaser(player, aimCourse: 64);
        Console.WriteLine(
            $"  intercept   : torpedo={shot.TargetIsTorpedo} hit={shot.Hit} "
            + $"damage={shot.TorpedoDamage} "
            + $"destroyed={shot.TorpedoDestroyed} "
            + $"beams={shot.BeamsFired} fuel={shot.FuelCost}");

        Check(shot.Fired && shot.Hit, "point-defense phaser fired and hit");
        Check(shot.TargetIsTorpedo,
            "phaser chose the closer torpedo over the ship");
        Check(shot.TargetSlot == -1, "no ship was targeted");
        Check(shot.TorpedoSlot == incoming.Slot,
            "the incoming torpedo was engaged");
        Check(shot.BeamsFired == 1, "interception spends a single beam");
        Check(shot.FuelCost == 240, "interception spends one beam of fuel");
        Check(shot.TorpedoDamage > incoming.Damage,
            "photon torpedo takes double beam damage");
        Check(shot.TorpedoDestroyed, "the warhead was consumed");

        var after = engine.CaptureSnapshot();
        var hitTorpedo = after.Torpedoes.Single(t => t.Slot == incoming.Slot);
        Check(hitTorpedo.IsDetonating, "intercepted torpedo detonates");
        Check(after.GetShip(player).BeamsLoaded == 3,
            "one phaser bank unloaded");
        Check(after.GetShip(player).Fuel == kirk.Fuel - 240,
            "one beam of fuel spent");
        Check(SumShields(after.GetShip(player)) == SumShields(kirk),
            "the blast was outside DETDIST");
        Check(SumShields(after.GetShip(enemy))
                == SumShields(before.GetShip(enemy)),
            "the hostile ship was not hit");

        engine.Step(11);
        Check(engine.CaptureSnapshot().TorpedoCount == 0,
            "interception cleared the torpedo slot");
    }

    // ------------------------------------------------------------------
    // Stage 2 ordnance profiles.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[ordnance profiles]");
    using (var engine = new DtrekEngine(seed: 1989))
    {
        var fed = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var rom = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 101000, 100000);
        var klingon = engine.Spawn(
            "Kor", DtrekTeam.Klingon, 102000, 100000);

        var before = engine.CaptureSnapshot();
        var photon = engine.GetOrdnanceSpec(DtrekWeaponClass.Photon);
        var missile = engine.GetOrdnanceSpec(DtrekWeaponClass.Missile);
        var plasma = engine.GetOrdnanceSpec(DtrekWeaponClass.Plasma);
        var chaff = engine.GetOrdnanceSpec(DtrekWeaponClass.Chaff);
        var mine = engine.GetOrdnanceSpec(DtrekWeaponClass.Mine);

        Console.WriteLine(
            $"  defaults    : fed={before.GetShip(fed).TorpedoClass} "
            + $"rom={before.GetShip(rom).TorpedoClass} "
            + $"klingon={before.GetShip(klingon).TorpedoClass}");
        Console.WriteLine(
            $"  photon      : {photon.Name} damage {photon.Damage} "
            + $"speed {photon.Speed} fuse {photon.Duration} "
            + $"reload {photon.Reload} fuel {photon.FuelCost}");
        Console.WriteLine(
            $"  missile     : {missile.Name} damage {missile.Damage} "
            + $"speed {missile.Speed} fuse {missile.Duration} "
            + $"tracking {missile.Tracking}");
        Console.WriteLine(
            $"  plasma      : {plasma.Name} damage {plasma.Damage} "
            + $"fuse {plasma.Duration} decay {plasma.Decay} "
            + $"fuel {plasma.FuelCost}");
        Console.WriteLine(
            $"  chaff/mine  : arm {chaff.Arm}/{mine.Arm} "
            + $"damage {chaff.Damage}/{mine.Damage}");

        Check(before.GetShip(fed).TorpedoClass == DtrekWeaponClass.Photon,
            "Federation ships default to photons");
        Check(before.GetShip(rom).TorpedoClass == DtrekWeaponClass.Plasma,
            "Romulan ships default to plasma");
        Check(before.GetShip(klingon).TorpedoClass == DtrekWeaponClass.Missile,
            "Klingon ships default to missiles");
        Check(before.GetShip(rom).Ordnance.Damage == 110,
            "snapshot carries the selected profile");
        Check(photon is
            {
                Speed: 14,
                Damage: 42,
                Duration: 20,
                Reload: 20,
                Decay: 0,
                FuelCost: 420,
                Arm: 0,
                Tracking: false,
            },
            "photon profile matches the Reliant defaults");
        Check(missile is { Speed: 8, Damage: 21, Tracking: true },
            "missile is fast, light, and guided");
        Check(plasma is
            {
                Speed: 8,
                Damage: 110,
                Duration: 100,
                Reload: 60,
                Decay: 8,
                Tracking: true,
            },
            "plasma is heavy, decaying, and guided");
        Check(chaff is { Damage: 19, Arm: 10, Tracking: false },
            "chaff is a delayed unguided cloud");
        Check(mine is { Speed: 0, Damage: 35, Arm: 20 },
            "mine is a stationary delayed warhead");

        engine.SetOrdnance(fed, DtrekWeaponClass.Chaff);
        var selected = engine.CaptureSnapshot().GetShip(fed);
        Check(selected.TorpedoClass == DtrekWeaponClass.Chaff
                && selected.Ordnance.Arm == 10,
            "training ships can select any ordnance class");

        try
        {
            engine.SetOrdnance(fed, DtrekWeaponClass.Phaser);
            Check(false, "beam classes cannot be loaded as ordnance");
        }
        catch (DtrekException ex) when (ex.Result == DtrekResult.InvalidArgument)
        {
            Check(true, "beam classes cannot be loaded as ordnance");
        }
    }

    // ------------------------------------------------------------------
    // Guided missiles.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[missile guidance]");
    using (var engine = new DtrekEngine(seed: 777))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 100000, 102000);

        engine.SetOrdnance(player, DtrekWeaponClass.Missile);
        var shot = engine.FireTorpedo(
            player, new TorpedoLaunch(64) { TargetSlot = enemy });

        Console.WriteLine(
            $"  launch      : class={shot.WeaponClass} target={shot.TargetSlot} "
            + $"tracking={shot.Tracking} damage={shot.Damage} "
            + $"speed={shot.Speed} fuse={shot.Fuse}");
        Check(shot.Fired, "locked missile left the tube");
        Check(shot.WeaponClass == DtrekWeaponClass.Missile,
            "missile class reported");
        Check(shot.TargetSlot == enemy && shot.Tracking,
            "missile carries the lock and tracks");
        Check(shot.Damage == 21 && shot.Speed == 8,
            "missile carries the light warhead");
        Check(shot.Fuse is >= 50 and <= 79,
            "missile fuse is duration plus up to 29 ticks");

        engine.Step(1);
        var guided = engine.CaptureSnapshot()
            .Torpedoes.Single(t => t.Slot == shot.TorpedoSlot);
        Console.WriteLine(
            $"  after 1     : course={guided.Course} "
            + $"position=({guided.X}, {guided.Y}) "
            + $"status={guided.Status}");
        Check(guided.Status == TorpedoStatus.Track,
            "missile is in tracking mode");
        Check(guided.Course == 128 && guided.Y > 100000,
            "missile turned south toward the locked target");

        var unguided = engine.FireTorpedo(player, new TorpedoLaunch(64));
        Check(unguided.Fired, "unguided missile launched");
        Check(!unguided.Tracking && unguided.TargetSlot == -1,
            "unguided missile ignores the lock");

        var selfLock = engine.FireTorpedo(
            player, new TorpedoLaunch(64) { TargetSlot = player });
        Check(!selfLock.Tracking && selfLock.TargetSlot == -1,
            "friendly lock is ignored");
    }

    // ------------------------------------------------------------------
    // Plasma torpedoes.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[plasma torpedo]");
    using (var engine = new DtrekEngine(seed: 31337))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 100000, 130000);

        engine.SetOrdnance(player, DtrekWeaponClass.Plasma);
        var before = engine.CaptureSnapshot().GetShip(player);
        var shot = engine.FireTorpedo(
            player, new TorpedoLaunch(64) { TargetSlot = enemy });

        Console.WriteLine(
            $"  launch      : class={shot.WeaponClass} damage={shot.Damage} "
            + $"fuse={shot.Fuse} fuel={shot.FuelCost} "
            + $"tracking={shot.Tracking}");
        Check(shot.Fired && shot.Tracking, "plasma left the tube tracking");
        Check(shot.Damage == 110 && shot.Speed == 8,
            "plasma carries the heavy warhead");
        Check(shot.Fuse == 100, "plasma fuse is fixed at one hundred ticks");
        Check(shot.FuelCost == 1100 && shot.TubesLoaded == 4,
            "plasma spends a heavy fuel load and one tube");

        var launched = engine.CaptureSnapshot();
        var torpedo = launched.Torpedoes.Single(t => t.Slot == shot.TorpedoSlot);
        Check(torpedo.BaseDamage == 110 && torpedo.Decay == 8,
            "snapshot exposes plasma base damage and decay");
        Check(launched.GetShip(player).Fuel == before.Fuel - 1100,
            "plasma fuel was deducted");
        Check(launched.GetShip(player).TubesReload == 60,
            "plasma reload interval is sixty ticks");

        engine.Step(10);
        var decayed = engine.CaptureSnapshot();
        var burning = decayed.Torpedoes.Single(t => t.Slot == shot.TorpedoSlot);
        Console.WriteLine(
            $"  after 10    : damage={burning.Damage} course={burning.Course}");
        Check(burning.Damage == 102, "plasma lost one decay step");
        Check(burning.Course == 128, "plasma turned toward the target");

        engine.Step(10);
        var twice = engine.CaptureSnapshot();
        Check(twice.Torpedoes.Single(t => t.Slot == shot.TorpedoSlot).Damage == 94,
            "plasma lost a second decay step");

        engine.Step(39);
        Check(engine.CaptureSnapshot().GetShip(player).TubesLoaded == 4,
            "plasma reload is still running at tick 59");
        engine.Step(1);
        Check(engine.CaptureSnapshot().GetShip(player).TubesLoaded == 5,
            "plasma tube reloaded at tick 60");
    }

    // ------------------------------------------------------------------
    // Chaff.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[chaff]");
    using (var engine = new DtrekEngine(seed: 5150))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        engine.SetControls(player, desiredCourse: 0, desiredSpeed: 4);
        engine.Step(30);
        engine.SetOrdnance(player, DtrekWeaponClass.Chaff);

        var before = engine.CaptureSnapshot();
        var kirk = before.GetShip(player);
        var shot = engine.FireTorpedo(player, course: 0);
        var cloud = engine.CaptureSnapshot()
            .Torpedoes.Single(t => t.Slot == shot.TorpedoSlot);
        var ahead = (int)Math.Round(Math.Sqrt(
            Math.Pow(cloud.X - kirk.X, 2) + Math.Pow(cloud.Y - kirk.Y, 2)));

        Console.WriteLine(
            $"  launch      : class={shot.WeaponClass} arm={shot.Arm} "
            + $"damage={shot.Damage} fuse={shot.Fuse} "
            + $"speed={cloud.Speed} ahead={ahead}");
        Check(shot.WeaponClass == DtrekWeaponClass.Chaff,
            "chaff class reported");
        Check(!shot.Tracking && shot.Arm == 10, "chaff is unguided and delayed");
        Check(cloud.Speed == kirk.Speed && cloud.Course == kirk.Course,
            "chaff inherits the ship's speed and heading");
        Check(ahead == DtrekConstants.TorpedoExplodeDistance * 2,
            "chaff starts eight hundred units ahead");
        Check(cloud.Status == TorpedoStatus.Straight, "chaff drifts straight");

        var target = engine.Spawn(
            "Cloud Target", DtrekTeam.Romulan, cloud.X, cloud.Y - 900);
        var shieldsBefore = SumShields(
            engine.CaptureSnapshot().GetShip(target));
        engine.Step(11);

        var after = engine.CaptureSnapshot();
        var drifting = after.Torpedoes.Single(t => t.Slot == shot.TorpedoSlot);
        Console.WriteLine(
            $"  detonation  : status={drifting.Status} "
            + $"shield loss={shieldsBefore - SumShields(after.GetShip(target))}");
        Check(drifting.Status == TorpedoStatus.Straight,
            "chaff keeps flying after doing damage");
        Check(SumShields(after.GetShip(target)) == shieldsBefore - 19,
            "the drifting cloud damaged the nearby hostile");
    }

    // ------------------------------------------------------------------
    // Mines.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[mine]");
    using (var engine = new DtrekEngine(seed: 9090))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 101000, 100000);

        engine.SetOrdnance(player, DtrekWeaponClass.Mine);
        var shot = engine.FireTorpedo(
            player, new TorpedoLaunch(64) { AimX = 101000, AimY = 100000 });
        var placed = engine.CaptureSnapshot();
        var mine = placed.Torpedoes.Single(t => t.Slot == shot.TorpedoSlot);
        var shieldsBefore = SumShields(placed.GetShip(enemy));

        Console.WriteLine(
            $"  placement   : ({mine.X}, {mine.Y}) status={mine.Status} "
            + $"speed={mine.Speed} arm={mine.Arm} damage={mine.Damage} "
            + $"fuse={mine.Fuse}");
        Check(shot.WeaponClass == DtrekWeaponClass.Mine,
            "mine class reported");
        Check(shot.Speed == 0 && shot.Arm == 20,
            "mine is stationary and slow to arm");
        Check(mine.X == 101000 && mine.Y == 100000,
            "mine sits at the aim point");

        engine.Step(20);
        Check(engine.CaptureSnapshot()
                .Torpedoes.Single(t => t.Slot == shot.TorpedoSlot)
                .Status == TorpedoStatus.Straight,
            "mine is still arming at tick 20");
        engine.Step(1);

        var boom = engine.CaptureSnapshot();
        Console.WriteLine(
            $"  detonation  : status="
            + $"{boom.Torpedoes.Single(t => t.Slot == shot.TorpedoSlot).Status} "
            + $"shield loss={shieldsBefore - SumShields(boom.GetShip(enemy))}");
        Check(boom.Torpedoes.Single(t => t.Slot == shot.TorpedoSlot)
                .Status == TorpedoStatus.Explode,
            "mine detonated after arming");
        Check(SumShields(boom.GetShip(enemy)) == shieldsBefore - 35,
            "mine damaged the nearby hostile");

        var defaultShot = engine.FireTorpedo(
            player, new TorpedoLaunch(64) { AimX = -1, AimY = -1 });
        var defaultMine = engine.CaptureSnapshot()
            .Torpedoes.Single(t => t.Slot == defaultShot.TorpedoSlot);
        var range = (int)Math.Round(Math.Sqrt(
            Math.Pow(defaultMine.X - 100000, 2)
            + Math.Pow(defaultMine.Y - 100000, 2)));
        Console.WriteLine(
            $"  default aim : ({defaultMine.X}, {defaultMine.Y}) range {range}");
        Check(range is >= 9999 and <= 10001,
            "default mine lands one mine range ahead");
        Check(defaultMine.X > 100000, "default mine lies along the launch course");
    }

    // ------------------------------------------------------------------
    // Class-specific point defense.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[ordnance point defense]");
    using (var engine = new DtrekEngine(seed: 1212))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 103000, 100000);

        engine.SetOrdnance(enemy, DtrekWeaponClass.Missile);
        var launch = engine.FireTorpedo(enemy, course: 192);
        engine.Step(5);

        var before = engine.CaptureSnapshot();
        var incoming = before.Torpedoes.Single(t => t.Slot == launch.TorpedoSlot);
        var kirk = before.GetShip(player);
        var range = (int)Math.Round(Math.Sqrt(
            Math.Pow(incoming.X - kirk.X, 2)
            + Math.Pow(incoming.Y - kirk.Y, 2)));
        var shot = engine.FirePhaser(player, aimCourse: 64);

        Console.WriteLine(
            $"  missile     : range={range} damage={shot.TorpedoDamage} "
            + $"destroyed={shot.TorpedoDestroyed}");
        Check(shot.TargetIsTorpedo, "missile selected for point defense");
        Check(shot.TorpedoDamage == 30 - range / 250,
            "missiles take normal beam damage");
        Check(shot.TorpedoDestroyed, "the light missile warhead was consumed");
    }

    using (var engine = new DtrekEngine(seed: 1313))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 103000, 100000);

        var launch = engine.FireTorpedo(enemy, course: 192);
        Check(launch.WeaponClass == DtrekWeaponClass.Plasma,
            "Romulan launches plasma by default");
        engine.Step(5);

        var before = engine.CaptureSnapshot();
        var incoming = before.Torpedoes.Single(t => t.Slot == launch.TorpedoSlot);
        var kirk = before.GetShip(player);
        var range = (int)Math.Round(Math.Sqrt(
            Math.Pow(incoming.X - kirk.X, 2)
            + Math.Pow(incoming.Y - kirk.Y, 2)));
        var shot = engine.FirePhaser(player, aimCourse: 64);

        var after = engine.CaptureSnapshot();
        var damaged = after.Torpedoes.Single(t => t.Slot == launch.TorpedoSlot);

        Console.WriteLine(
            $"  plasma      : range={range} damage={shot.TorpedoDamage} "
            + $"destroyed={shot.TorpedoDestroyed} "
            + $"remaining={damaged.Damage}");
        Check(shot.TargetIsTorpedo, "plasma selected for point defense");
        Check(shot.TorpedoDamage == 30 / 2 - range / 250,
            "plasma takes half beam damage");
        Check(!shot.TorpedoDestroyed, "the heavy plasma warhead survives");
        Check(damaged.Damage == 110 - shot.TorpedoDamage,
            "plasma warhead was reduced by the beam");
    }

    // ------------------------------------------------------------------
    // Stage 3 galaxy generation.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[galaxy]");
    using (var engine = new DtrekEngine(seed: 1989))
    {
        Check(engine.CaptureSnapshot().PlanetCount == 0,
            "engines start with an empty map");

        engine.GenerateGalaxy(1989);
        var galaxy = engine.CaptureSnapshot();
        var earth = galaxy.FindPlanet(0);
        var romulus = galaxy.FindPlanet(10);
        var klingus = galaxy.FindPlanet(20);
        var orion = galaxy.FindPlanet(30);
        var center = galaxy.FindPlanet(40);
        var wanderer = galaxy.FindPlanet(41);

        Console.WriteLine(
            $"  objects     : {galaxy.PlanetCount} planets and features");
        Console.WriteLine(
            $"  homes       : Earth ({earth?.X}, {earth?.Y})  "
            + $"Romulus ({romulus?.X}, {romulus?.Y})");
        Console.WriteLine(
            $"  center      : {center?.Name} at ({center?.X}, {center?.Y})  "
            + $"wanderer {wanderer?.Name}");

        Check(galaxy.PlanetCount == DtrekConstants.MaxPlanets,
            "galaxy has eighty objects");
        Check(earth is
            {
                Owner: DtrekTeam.Federation,
                X: 44444,
                Y: 155556,
            },
            "Earth sits at the Federation home");
        Check(earth is not null
                && earth.IsHomeWorld
                && earth.IsFuelDepot
                && earth.IsRepairBase,
            "Earth is a home fuel and repair world");
        Check(romulus is { X: 44444, Y: 44444 },
            "Romulus sits at the Romulan home");
        Check(klingus is { X: 155556, Y: 44444 },
            "Klingus sits at the Klingon home");
        Check(orion is { X: 155556, Y: 155556 },
            "Orion sits at the Orion home");
        Check(center is
            {
                Owner: DtrekTeam.Independent,
                X: 100000,
                Y: 100000,
            },
            "independent home sits at the galactic center");
        Check(wanderer is not null && wanderer.IsWanderer,
            "slot 41 is a wanderer");
        Check(galaxy.Planets.Any(p => p.IsBlackHole),
            "galaxy contains black holes");
        Check(galaxy.Planets.Any(p => p.IsPulsar),
            "galaxy contains pulsars");
        Check(galaxy.Planets.Any(p => p.IsNebula),
            "galaxy contains nebulae");

        var before = engine.CaptureSnapshot();
        engine.Step(1);
        var after = engine.CaptureSnapshot();
        var wandererBefore = before.FindPlanet(41);
        var wandererAfter = after.FindPlanet(41);

        Check(wandererBefore is not null
                && wandererAfter is not null
                && (wandererBefore.X != wandererAfter.X
                    || wandererBefore.Y != wandererAfter.Y),
            "wanderers drift every tick");
        Check(after.PlanetCount == DtrekConstants.MaxPlanets,
            "the galaxy survives a tick");

        engine.ClearPlanets();
        Check(engine.CaptureSnapshot().PlanetCount == 0,
            "ClearPlanets empties the map");
    }

    using (var first = new DtrekEngine(seed: 1))
    using (var second = new DtrekEngine(seed: 2))
    {
        first.GenerateGalaxy(1989);
        second.GenerateGalaxy(1989);
        var a = first.CaptureSnapshot();
        var b = second.CaptureSnapshot();

        static IEnumerable<(int, int, int, string)> Shape(GameSnapshot g)
            => g.Planets.Select(p => (p.Slot, p.X, p.Y, p.Name));

        Check(Shape(a).SequenceEqual(Shape(b)),
            "the same galaxy seed produces the same map");

        second.GenerateGalaxy(2001);
        var other = second.CaptureSnapshot();
        Check(!Shape(a).SequenceEqual(Shape(other)),
            "a different galaxy seed produces a different map");
    }

    // ------------------------------------------------------------------
    // Stage 3 orbit, fuel, and repair services.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[orbit and services]");
    using (var engine = new DtrekEngine(seed: 5))
    {
        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.Fuel | DtrekPlanetFlags.Repair,
            DtrekTeam.Federation,
            armies: 10,
            x: 100000,
            y: 100000,
            name: "Starbase");
        var ship = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100400);

        var entered = engine.Orbit(ship);
        var orbit = engine.CaptureSnapshot().GetShip(ship);
        var radius = (int)Math.Round(Math.Sqrt(
            Math.Pow(orbit.X - 100000, 2)
            + Math.Pow(orbit.Y - 100000, 2)));

        Console.WriteLine(
            $"  orbit       : planet {entered} at ({orbit.X}, {orbit.Y}) "
            + $"radius {radius} speed {orbit.Speed}");
        Check(entered == 0, "orbit finds the nearest planet");
        Check(orbit.IsOrbiting && orbit.OrbitPlanetSlot == 0,
            "ship entered orbit");
        Check(orbit.AtFuelPlanet && orbit.AtRepairPlanet,
            "friendly fuel and repair services detected");
        Check(radius >= 700, "orbit radius holds at about eight hundred units");

        var orbitX = orbit.X;
        var orbitY = orbit.Y;
        engine.Step(1);
        var rotating = engine.CaptureSnapshot().GetShip(ship);
        Check(rotating.X != orbitX || rotating.Y != orbitY,
            "ship rotates in orbit");
        Check(rotating.Speed == 0, "orbiting ship stays at rest");

        engine.LeaveOrbit(ship);
        var free = engine.CaptureSnapshot().GetShip(ship);
        Check(!free.IsOrbiting && free.OrbitPlanetSlot == -1,
            "leaving orbit clears the orbit");
    }

    using (var engine = new DtrekEngine(seed: 7))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100200);
        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 100000, 100400);

        var fired = engine.FirePhaser(player, aimCourse: 128);
        Check(fired.Fired && fired.Hit, "Kirk hits the Romulan");
        Check(fired.BeamsFired == 4, "Kirk's volley uses four banks");
        Check(fired.Damage == 120, "close-range volley damage is 120");

        var afterVolley = engine.CaptureSnapshot().GetShip(player);
        Check(afterVolley.BeamsLoaded == 0,
            "the volley unloaded the phaser banks");
        Check(afterVolley.Fuel == 7540, "beam fuel consumption");

        /*
         * Kirk's full volley may knock out enemy banks (the authentic Stage 5
         * spill), so the return fire is weaker.  Drop the player's shields so
         * it reaches the hull and sets up repair.
         */
        engine.SetShields(player, up: false);
        var returned = engine.FirePhaser(enemy, aimCourse: 0);
        Check(returned.Fired && returned.Hit, "the Romulan returns fire");

        var damaged = engine.CaptureSnapshot().GetShip(player);
        Check(damaged.Hull < damaged.HullMaximum,
            "return fire damages the hull");
        Check(damaged.DamagedSystemCount > 0,
            "return fire damages subsystems");

        var hullBefore = damaged.Hull;
        var fuelBefore = damaged.Fuel;

        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.Fuel | DtrekPlanetFlags.Repair,
            DtrekTeam.Federation,
            armies: 10,
            x: damaged.X,
            y: damaged.Y,
            name: "Starbase");
        engine.Orbit(player, 0);
        engine.SetRepair(player, on: true);
        engine.Step(10);

        var serviced = engine.CaptureSnapshot().GetShip(player);
        Console.WriteLine(
            $"  service     : hull {hullBefore}->{serviced.Hull}  "
            + $"fuel {fuelBefore}->{serviced.Fuel}  "
            + $"beams {serviced.BeamsLoaded}/{serviced.BeamsTotal}  "
            + $"tubes {serviced.TubesLoaded}/{serviced.TubesTotal}");
        Check(serviced.RepairMode, "repair mode is enabled");
        Check(serviced.Hull == hullBefore,
            "subsystem repair waits for the thirty-tick fuse");
        Check(serviced.Fuel == Math.Min(fuelBefore + 280, serviced.FuelMaximum),
            "friendly fuel planet adds six per tick");
        Check(serviced.BeamsLoaded == serviced.BeamsTotal,
            "repair planet rearms the surviving phaser banks");
        Check(serviced.TubesLoaded == serviced.TubesTotal,
            "repair planet rearms the surviving torpedo tubes");
        Check(serviced.AtFuelPlanet && serviced.AtRepairPlanet,
            "services report active in the snapshot");

        engine.Step(20);
        var repaired = engine.CaptureSnapshot().GetShip(player);
        Check(repaired.Hull == hullBefore + 16,
            "repair planet restores sixteen hull points per pulse");
    }

    // ------------------------------------------------------------------
    // Stage 3 hazards and planetary torpedo impact.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[hazards]");
    using (var engine = new DtrekEngine(seed: 8))
    {
        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.BlackHole,
            DtrekTeam.Independent,
            armies: 0,
            x: 100000,
            y: 100000,
            name: "Void");
        var ship = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 106000, 100000);
        var xBefore = engine.CaptureSnapshot().GetShip(ship).X;
        engine.Step(1);
        var pulled = engine.CaptureSnapshot().GetShip(ship);
        Console.WriteLine(
            $"  black hole  : x {xBefore}->{pulled.X}  status {pulled.Status}");
        Check(pulled.X < xBefore, "black hole gravity pulls the ship in");
        Check(pulled.Status == DtrekPlayerStatus.Alive,
            "a distant ship survives black hole gravity");

        var doomed = engine.Spawn(
            "Doomed", DtrekTeam.Federation, 100100, 100000);
        engine.Step(1);
        Check(engine.CaptureSnapshot().GetShip(doomed).Status
                != DtrekPlayerStatus.Alive,
            "a ship touching the black hole is destroyed");

        engine.ClearPlanets();
        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.Pulsar,
            DtrekTeam.Independent,
            armies: 0,
            x: 100000,
            y: 100000,
            name: "Pulse");
        var burned = engine.Spawn(
            "Burned", DtrekTeam.Federation, 100500, 100000);
        engine.Step(1);
        Check(engine.CaptureSnapshot().GetShip(burned).Status
                != DtrekPlayerStatus.Alive,
            "a pulsar destroys a nearby ship");
    }

    using (var engine = new DtrekEngine(seed: 9))
    {
        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.None,
            DtrekTeam.Independent,
            armies: 0,
            x: 100000,
            y: 98000,
            name: "Target");
        var ship = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);

        engine.FireTorpedo(ship, course: 0);
        engine.Step(6);
        Check(engine.CaptureSnapshot().TorpedoCount == 0,
            "torpedoes are removed on planetary impact");

        engine.ClearPlanets();
        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.Nebula,
            DtrekTeam.Independent,
            armies: 0,
            x: 100000,
            y: 98000,
            name: "Cloud");
        engine.FireTorpedo(ship, course: 0);
        engine.Step(5);
        Check(engine.CaptureSnapshot().TorpedoCount > 0,
            "nebulae do not stop torpedoes");
    }

    // ------------------------------------------------------------------
    // Stage 4 robot AI.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[robot metadata]");
    using (var engine = new DtrekEngine(seed: 21))
    {
        Console.WriteLine(
            $"  library     : ABI {DtrekEngine.NativeAbiVersion}  "
            + $"{DtrekEngine.NativeVersion}");
        Check(DtrekEngine.NativeAbiVersion == 6, "managed and native ABI 6 agree");
        Check(DtrekEngine.NativeVersion == "dtrek_core 0.6.0",
            "native core reports version 0.6.0");

        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 90000, 90000);
        var robotSlot = engine.SpawnRobot(
            "Warbird", DtrekTeam.Romulan, 100000, 100000);
        var snapshot = engine.CaptureSnapshot();
        var robot = snapshot.GetShip(robotSlot);

        Console.WriteLine(
            $"  defaults    : offense {robot.RobotOffense}  "
            + $"impatience {robot.RobotImpatience}  "
            + $"attack speed {robot.RobotAttackSpeed}  state {robot.RobotState}");
        Check(robot.IsRobot, "spawned robot is marked as a robot");
        Check(robot.RobotFlags == DtrekRobotFlags.None,
            "robot starts without special flags");
        Check(robot.RobotOffense is >= 70 and <= 90
                && robot.RobotOffense % 10 == 0,
            "robot uses the original automatic offense");
        Check(robot.RobotImpatience is >= 50 and <= 100,
            "robot uses the original automatic impatience");
        Check(robot.RobotAttackSpeed == 5, "Romulan robot attack speed");
        Check(robot.RobotState == DtrekRobotState.Idle,
            "robot starts idle");
        Check(robot.RobotTargetSlot == -1,
            "robot starts without a target");
        Check(robot.RobotPatrolPlanetSlot == -1,
            "robot starts without a patrol planet");
        Check(robot.RobotLastBeamTick == -1,
            "robot has not fired a beam");
        Check(robot.RobotLastTorpedoTick == -1,
            "robot has not fired a torpedo");
        Check(!snapshot.GetShip(player).IsRobot,
            "ordinary ships are not robots");

        var orbiterSlot = engine.SpawnRobot(
            "Sentinel",
            DtrekTeam.Federation,
            120000,
            120000,
            new DtrekRobotConfig
            {
                Offense = 100,
                Impatience = 100,
                Flags = DtrekRobotFlags.Orbiter,
            });
        var orbiter = engine.CaptureSnapshot().GetShip(orbiterSlot);
        Check(orbiter.RobotOffense == 100 && orbiter.RobotImpatience == 100,
            "explicit robot tuning is stored");
        Check(orbiter.RobotFlags == DtrekRobotFlags.Orbiter,
            "orbiter flag is stored");
        Check(orbiter.RobotAttackSpeed == 7,
            "orbiter inherits the Federation attack speed");

        engine.SetRobot(robotSlot, new DtrekRobotConfig
        {
            Offense = 55,
            Impatience = 66,
            AttackSpeed = 3,
        });
        robot = engine.CaptureSnapshot().GetShip(robotSlot);
        Check(robot.RobotOffense == 55
                && robot.RobotImpatience == 66
                && robot.RobotAttackSpeed == 3,
            "SetRobot retunes an existing robot");

        var rejected = 0;
        foreach (var bad in new[]
                 {
                     new DtrekRobotConfig { Offense = 101 },
                     new DtrekRobotConfig { Impatience = 101 },
                     new DtrekRobotConfig { AttackSpeed = 13 },
                     new DtrekRobotConfig { Flags = (DtrekRobotFlags)4 },
                     new DtrekRobotConfig { PatrolPlanet = DtrekConstants.MaxPlanets },
                 })
        {
            try
            {
                engine.SpawnRobot(
                    "Bad", DtrekTeam.Federation, 100000, 100000, bad);
            }
            catch (ArgumentOutOfRangeException)
            {
                rejected++;
            }
        }

        Check(rejected == 5, "invalid robot tuning is rejected");

        var nonRobotRejected = 0;
        try
        {
            engine.SetRobot(player, new DtrekRobotConfig { Offense = 50 });
        }
        catch (DtrekException ex) when (ex.Result == DtrekResult.InvalidState)
        {
            nonRobotRejected++;
        }

        try
        {
            engine.ClearRobot(player);
        }
        catch (DtrekException ex) when (ex.Result == DtrekResult.InvalidState)
        {
            nonRobotRejected++;
        }

        Check(nonRobotRejected == 2,
            "robot commands reject ordinary ships");
    }

    Console.WriteLine();
    Console.WriteLine("[robot combat]");
    using (var engine = new DtrekEngine(seed: 22))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 100000, 100000);
        var robotSlot = engine.SpawnRobot(
            "Warbird",
            DtrekTeam.Romulan,
            102000,
            100000,
            new DtrekRobotConfig
            {
                Offense = 100,
                Impatience = 100,
                AttackSpeed = 6,
            });

        var before = engine.CaptureSnapshot();
        var initialShields = SumShields(before.GetShip(player));

        engine.Step(1);
        var engaged = engine.CaptureSnapshot();
        var robot = engaged.GetShip(robotSlot);
        var launched = engaged.Torpedoes
            .FirstOrDefault(t => t.Slot == robot.RobotLastTorpedoSlot);

        Console.WriteLine(
            $"  engage      : target {robot.RobotTargetSlot}  "
            + $"state {robot.RobotState}  "
            + $"torpedo {robot.RobotLastTorpedoSlot}");
        Check(robot.RobotTargetSlot == player, "robot locks the nearest enemy");
        Check(robot.RobotState == DtrekRobotState.Attack,
            "robot enters the attack state");
        Check(robot.RobotLastTorpedoTick == 1,
            "robot fires a torpedo on its first update");
        Check(robot.RobotLastTorpedoTargetSlot == player,
            "robot torpedo locks the player");
        Check(launched is
            {
                WeaponClass: DtrekWeaponClass.Plasma,
                TargetSlot: var target,
            } && target == player,
            "Romulan robot launches homing plasma");

        engine.Step(9);
        robot = engine.CaptureSnapshot().GetShip(robotSlot);
        Check(robot.RobotLastBeamTick > 0, "robot fires a phaser volley");

        engine.Step(30);
        var damaged = engine.CaptureSnapshot().GetShip(player);
        Check(damaged.Status != DtrekPlayerStatus.Alive
                || SumShields(damaged) < initialShields
                || damaged.Hull < damaged.HullMaximum,
            "robot weapons damage the player");
    }

    Console.WriteLine();
    Console.WriteLine("[robot relaxed start]");
    using (var engine = new DtrekEngine(seed: 1989))
    {
        engine.GenerateGalaxy(1989);
        var player = engine.Spawn(
            "Reliant", DtrekTeam.Federation, 100000, 102000);
        var robotSlot = engine.SpawnRobot(
            "Warbird",
            DtrekTeam.Romulan,
            124000,
            102000,
            new DtrekRobotConfig
            {
                Offense = 50,
                Impatience = 50,
                AttackSpeed = 3,
            });

        engine.Step(1);
        var start = engine.CaptureSnapshot();
        var robot = start.GetShip(robotSlot);
        var playerShip = start.GetShip(player);
        var startDistance = (int)Math.Sqrt(
            Math.Pow(playerShip.X - robot.X, 2)
            + Math.Pow(playerShip.Y - robot.Y, 2));
        Console.WriteLine(
            $"  relaxed start: state {robot.RobotState}  "
            + $"target {robot.RobotTargetSlot}  "
            + $"offense {robot.RobotOffense}  "
            + $"distance {startDistance}");
        Check(robot.RobotTargetSlot == player,
            "relaxed robot still acquires the player");
        Check(robot.RobotState != DtrekRobotState.Attack,
            "relaxed robot does not attack at patrol start");
        Check(robot.RobotLastTorpedoTick == -1,
            "relaxed robot holds its torpedoes");
        Check(startDistance > 20000,
            "relaxed robot starts outside torpedo range");

        engine.Step(119);
        var settled = engine.CaptureSnapshot();
        var settledPlayer = settled.GetShip(player);
        Check(settled.Tick == 120, "relaxed start advanced 12 seconds");
        Check(settled.TorpedoCount == 0,
            "relaxed start launches no torpedoes");
        Check(settledPlayer.Hull == playerShip.Hull,
            "relaxed start leaves the player unharmed");
    }

    Console.WriteLine();
    Console.WriteLine("[robot evasion]");
    using (var engine = new DtrekEngine(seed: 23))
    {
        var player = engine.Spawn(
            "Kirk", DtrekTeam.Federation, 103000, 100000);
        var robotSlot = engine.SpawnRobot(
            "Warbird",
            DtrekTeam.Romulan,
            100000,
            100000,
            new DtrekRobotConfig
            {
                Offense = 100,
                Impatience = 100,
                AttackSpeed = 5,
            });

        for (var i = 0; i < 5; ++i)
        {
            engine.FireTorpedo(player, course: 192);
        }

        engine.Step(1);
        var robot = engine.CaptureSnapshot().GetShip(robotSlot);
        Console.WriteLine(
            $"  evade       : state {robot.RobotState}  "
            + $"fuse {robot.RobotEvasionFuse}");
        Check(robot.RobotState == DtrekRobotState.Evade,
            "robot evades an incoming torpedo salvo");
        Check(robot.RobotEvasionFuse > 0, "robot evasion fuse is running");
    }

    Console.WriteLine();
    Console.WriteLine("[robot repair]");
    using (var engine = new DtrekEngine(seed: 24))
    {
        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.Fuel | DtrekPlanetFlags.Repair,
            DtrekTeam.Federation,
            armies: 10,
            x: 100000,
            y: 100800,
            name: "Starbase");
        var robotSlot = engine.SpawnRobot(
            "Sentinel",
            DtrekTeam.Federation,
            100000,
            100800,
            new DtrekRobotConfig
            {
                Offense = 100,
                Impatience = 100,
                AttackSpeed = 4,
                Flags = DtrekRobotFlags.Orbiter,
            });

        var orbiting = engine.CaptureSnapshot().GetShip(robotSlot);
        Check(orbiting.IsOrbiting && orbiting.OrbitPlanetSlot == 0,
            "orbiter robot enters orbit");

        var enemy = engine.Spawn(
            "Targ", DtrekTeam.Romulan, 103000, 100000);
        engine.SetShields(robotSlot, up: false);
        var shot = engine.FirePhaser(enemy, aimCourse: 192);
        var damaged = engine.CaptureSnapshot().GetShip(robotSlot);
        var hullDamaged = damaged.Hull;
        Check(shot.Hit && hullDamaged < damaged.HullMaximum,
            "attacker damages the orbiting robot");

        engine.Step(30);
        var repaired = engine.CaptureSnapshot().GetShip(robotSlot);
        Console.WriteLine(
            $"  repair      : state {repaired.RobotState}  "
            + $"hull {hullDamaged}->{repaired.Hull}  "
            + $"shields up {repaired.ShieldsUp}");
        Check(repaired.RobotState == DtrekRobotState.Repair,
            "robot enters the repair state");
        Check(repaired.RepairMode, "robot enables damage control");
        Check(!repaired.ShieldsUp, "robot drops shields to repair");
        Check(repaired.Hull > hullDamaged,
            "robot repairs damage at the starbase");
    }

    Console.WriteLine();
    Console.WriteLine("[robot patrol]");
    using (var engine = new DtrekEngine(seed: 25))
    {
        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.Fuel,
            DtrekTeam.Federation,
            armies: 10,
            x: 110000,
            y: 100000,
            name: "Patrol Base");
        var robotSlot = engine.SpawnRobot(
            "Sentinel",
            DtrekTeam.Federation,
            100000,
            100000,
            new DtrekRobotConfig
            {
                Offense = 50,
                Impatience = 50,
                AttackSpeed = 4,
            });

        var start = engine.CaptureSnapshot().GetShip(robotSlot);
        var startX = start.X;
        var startY = start.Y;

        engine.Step(1);
        var patrolling = engine.CaptureSnapshot().GetShip(robotSlot);
        Console.WriteLine(
            $"  patrol      : planet {patrolling.RobotPatrolPlanetSlot}  "
            + $"course {patrolling.DesiredCourse}  "
            + $"speed {patrolling.DesiredSpeed}");
        Check(patrolling.RobotState == DtrekRobotState.Patrol,
            "idle robot patrols");
        Check(patrolling.RobotPatrolPlanetSlot == 0,
            "robot selects a friendly patrol planet");
        Check(patrolling.RobotPatrolFuse > 0,
            "robot patrol fuse is running");
        Check(patrolling.DesiredSpeed == 4, "robot patrols at attack speed");
        Check(patrolling.DesiredCourse is > 16 and < 96,
            "robot heads toward the patrol planet");

        engine.Step(30);
        var moved = engine.CaptureSnapshot().GetShip(robotSlot);
        var travelled = (int)Math.Sqrt(
            Math.Pow(moved.X - startX, 2) + Math.Pow(moved.Y - startY, 2));
        Check(travelled > 100, "robot moves on patrol");
        Check(moved.RobotState == DtrekRobotState.Patrol,
            "robot stays on patrol");
    }

    Console.WriteLine();
    Console.WriteLine("[robot lifecycle]");
    using (var engine = new DtrekEngine(seed: 26))
    {
        var robotSlot = engine.SpawnRobot(
            "Sentinel",
            DtrekTeam.Federation,
            100000,
            100000,
            new DtrekRobotConfig
            {
                Offense = 60,
                Impatience = 60,
                AttackSpeed = 4,
            });

        engine.FireTorpedo(robotSlot, course: 64);
        var armed = engine.CaptureSnapshot();
        Check(armed.ShipCount == 1 && armed.TorpedoCount > 0,
            "robot has a torpedo in flight");

        engine.ClearRobot(robotSlot);
        var cleared = engine.CaptureSnapshot();
        Check(!cleared.GetShip(robotSlot).InUse,
            "cleared robot slot is free");
        Check(cleared.ShipCount == 0, "cleared robot leaves the ship count");
        Check(cleared.TorpedoCount == 0,
            "cleared robot torpedoes are removed");
        engine.Step(5);
        Check(engine.CaptureSnapshot().Tick == 5,
            "engine steps cleanly after clearing a robot");
    }

    var firstRun = RobotScenarioDigest(27);
    var secondRun = RobotScenarioDigest(27);
    var otherRun = RobotScenarioDigest(28);
    Check(firstRun == secondRun,
        "the same robot seed produces the same simulation");
    Check(firstRun != otherRun,
        "a different robot seed produces a different simulation");

    // ------------------------------------------------------------------
    // Stage 5 subsystem damage and repair.
    // ------------------------------------------------------------------
    Console.WriteLine();
    Console.WriteLine("[subsystem damage]");
    using (var engine = new DtrekEngine(seed: 1234))
    {
        var player = engine.Spawn(
            "Shooter", DtrekTeam.Federation, 100000, 100000);
        var enemy = engine.Spawn(
            "Victim", DtrekTeam.Klingon, 103000, 100000);
        engine.SetShields(enemy, up: false);

        var initial = engine.CaptureSnapshot().GetShip(enemy);
        Console.WriteLine(
            $"  hit table   : {initial.SystemCount} units  "
            + $"{initial.SystemSize} ktons  "
            + $"armor {initial.Armor}/{initial.ArmorMaximum}");
        Check(initial.SystemCount == 15, "the hit table holds fifteen units");
        Check(initial.SystemSize == 105,
            "hit table size excludes armor and hull");

        var armor = initial.FindSystem(DtrekSystemItem.Armor);
        var hullUnit = initial.FindSystem(DtrekSystemItem.Hull);
        var engineUnit = initial.FindSystem(DtrekSystemItem.Engine);
        var fuel = initial.FindSystem(DtrekSystemItem.Fuel);
        var beam = initial.FindSystem(DtrekSystemItem.BeamNumber, 2);
        var tube = initial.FindSystem(DtrekSystemItem.TorpedoNumber, 4);
        var dilithium = initial.FindSystem(DtrekSystemItem.Dilithium);
        var cargo = initial.FindSystem(DtrekSystemItem.Cargo);
        Check(armor is { Size: 1, SizeMaximum: 1, RepairRate: 0,
                PlanetRepairRate: 1500 },
            "armor repairs only at a base");
        Check(hullUnit is { Size: 83, RepairRate: 562,
                PlanetRepairRate: 3000 },
            "hull carries the original self and base rates");
        Check(engineUnit is { Size: 27, RepairRate: 140,
                PlanetRepairRate: 450 },
            "engine carries the original self and base rates");
        Check(fuel is { Size: 17, RepairRate: 112, PlanetRepairRate: 450 },
            "fuel carries the original self and base rates");
        Check(beam is { Size: 6, RepairRate: 168, PlanetRepairRate: 540 },
            "beam banks carry the original self and base rates");
        Check(tube is { Size: 5, RepairRate: 84, PlanetRepairRate: 270 },
            "torpedo tubes carry the original self and base rates");
        Check(dilithium is { Size: 9, RepairRate: 39, PlanetRepairRate: 150 },
            "dilithium carries the original self and base rates");
        Check(cargo is { Size: 3, RepairRate: 168, PlanetRepairRate: 450 },
            "cargo carries the original self and base rates");
        Check(!initial.HasSystemDamage, "a fresh ship has no system damage");

        var shot = engine.FirePhaser(player, aimCourse: 64);
        Check(shot.ArmorAbsorbed == 1, "armor absorbs the first point");
        Check(shot.HullDamage == 71, "hull takes the remainder");

        var damaged = engine.CaptureSnapshot().GetShip(enemy);
        Console.WriteLine(
            $"  damage      : armor {damaged.Armor}  hull {damaged.Hull}  "
            + $"systems {damaged.SystemSize}/105  "
            + $"damaged units {damaged.DamagedSystemCount}");
        Check(damaged.Armor == 0, "armor is gone");
        Check(damaged.Hull == 12, "hull dropped to twelve");
        Check(damaged.SystemSize < 105,
            "the full post-armor damage spills over the systems");
        Check(damaged.DamagedSystemCount > 0, "subsystems took damage");

        Check(damaged.FindSystem(DtrekSystemItem.Hull)!.Size == 12,
            "the hull unit matches the ship hull");
        var engineAfter = damaged.FindSystem(DtrekSystemItem.Engine)!;
        if (engineAfter.IsDamaged)
        {
            Check(damaged.EngineHealth
                    == engineAfter.Size * 256 / engineAfter.SizeMaximum,
                "engine condition follows the engine unit");
        }

        var intactBeams = damaged.Systems.Count(system =>
            system.Item == DtrekSystemItem.BeamNumber && !system.IsDamaged);
        var intactTubes = damaged.Systems.Count(system =>
            system.Item == DtrekSystemItem.TorpedoNumber && !system.IsDamaged);
        Check(damaged.BeamsTotal == intactBeams,
            "destroyed beam banks leave the ship");
        Check(damaged.TubesTotal == intactTubes,
            "destroyed torpedo tubes leave the ship");
        var systemSize = damaged.Systems
            .Where(system => system.Item != DtrekSystemItem.Armor
                && system.Item != DtrekSystemItem.Hull)
            .Sum(system => system.Size);
        Check(damaged.SystemSize == systemSize,
            "system size tracks the surviving units");

        var hullBefore = damaged.Hull;
        var armorBefore = damaged.Armor;
        engine.Step(29);
        var preFuse = engine.CaptureSnapshot().GetShip(enemy);
        Check(preFuse.Hull == hullBefore, "no repair before the repair fuse");
        engine.Step(1);
        var fused = engine.CaptureSnapshot().GetShip(enemy);
        Console.WriteLine(
            $"  self repair : hull {hullBefore}->{fused.Hull}  "
            + $"armor {armorBefore}->{fused.Armor}");
        Check(fused.Hull == hullBefore + 2,
            "self repair restores two hull points");
        Check(fused.Armor == armorBefore, "armor cannot self repair");

        engine.PlacePlanet(
            0,
            DtrekPlanetFlags.Repair,
            DtrekTeam.Klingon,
            armies: 0,
            x: fused.X,
            y: fused.Y,
            name: "Repair Base");
        engine.Orbit(enemy, 0);
        engine.SetRepair(enemy, on: true);
        hullBefore = fused.Hull;
        armorBefore = fused.Armor;
        engine.Step(29);
        Check(engine.CaptureSnapshot().GetShip(enemy).Hull == hullBefore,
            "base repair also waits for the fuse");
        engine.Step(1);
        var based = engine.CaptureSnapshot().GetShip(enemy);
        Console.WriteLine(
            $"  base repair : hull {hullBefore}->{based.Hull}  "
            + $"armor {armorBefore}->{based.Armor}");
        Check(based.Hull == hullBefore + 16,
            "base repair adds the planet hull rate");
        Check(based.Armor == armorBefore + 1,
            "base repair restores one armor point");
    }

    Console.WriteLine();
    if (failures == 0)
    {
        Console.WriteLine($"=== SMOKE TEST PASSED ({checks} checks) ===");
        return 0;
    }

    Console.WriteLine($"=== SMOKE TEST FAILED ({failures} of {checks}) ===");
    return 1;
}
catch (DllNotFoundException ex)
{
    Console.Error.WriteLine("Native core not found: " + ex.Message);
    Console.Error.WriteLine(
        "Build it first with scripts\\build.ps1 or the CMake command in README.md.");
    return 2;
}
catch (Exception ex)
{
    Console.Error.WriteLine($"Unexpected failure: {ex.GetType().Name}: {ex.Message}");
    Console.Error.WriteLine(ex.StackTrace);
    return 3;
}
