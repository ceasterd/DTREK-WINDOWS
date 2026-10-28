using Dtrek.Core;

namespace Dtrek.App;

internal readonly record struct InputState(
    bool TurnLeft,
    bool TurnRight,
    bool Accelerate,
    bool Decelerate);

public readonly record struct BeamEffect(
    int X1,
    int Y1,
    int X2,
    int Y2,
    bool Hit,
    bool FromPlayer,
    int ExpiresAtTick);

/// <summary>
/// Drives one patrol: player input, the native-core robot hostile, torpedo
/// launches, and the beam/event bookkeeping the renderer and HUD need.
/// </summary>
internal sealed class GameSession : IDisposable
{
    // Matches the Reliant-class beam range in the native core.
    private const int BeamRange = 7500;
    private const int PlayerStartX = 100000;
    private const int PlayerStartY = 102000;

    // Opening encounter tuning.  The robot starts well outside torpedo range
    // and is deliberately less aggressive, so the player has time to orient
    // before the fight begins.  Move the spawn closer or raise these values
    // for a harder opening.
    private const int EnemyStartX = 124000;
    private const int EnemyStartY = 102000;
    private static readonly DtrekRobotConfig EnemyRobotConfig = new()
    {
        Offense = 50,
        Impatience = 50,
        AttackSpeed = 3,
    };
    private static readonly DtrekRobotConfig KlingonRobotConfig = new()
    {
        Offense = 65,
        Impatience = 65,
        AttackSpeed = 4,
    };
    private static readonly DtrekRobotConfig OrionRobotConfig = new()
    {
        Offense = 60,
        Impatience = 75,
        AttackSpeed = 6,
    };
    private static readonly DtrekRobotConfig RomulanHeavyConfig = new()
    {
        Offense = 85,
        Impatience = 85,
        AttackSpeed = 5,
    };
    private static readonly DtrekRobotConfig PatrolRobotConfig = new()
    {
        Offense = 55,
        Impatience = 55,
        AttackSpeed = 4,
    };

    private const int MaxLogEntries = 40;
    private const int LockRange = 30000;

    private readonly DtrekEngine _engine;
    private readonly List<BeamEffect> _beams = [];
    private readonly List<string> _log = [];
    private readonly HashSet<int> _detonationsLogged = [];
    private readonly HashSet<(DtrekSystemItem Item, int Subitem)>
        _damagedSystems = [];

    private int _course = 64;
    private int _speed;
    private int _speedCooldown;
    private int _lastRobotBeamTick = -1;
    private int _lastRobotTorpedoTick = -1;
    private bool _locked;
    private readonly List<int> _robotSlots = [];
    private DtrekTeam _playerTeam = DtrekTeam.Federation;

    public GameSession(uint seed)
    {
        Seed = seed;
        _engine = new DtrekEngine(seed);
        _robotSlots.Clear();
        _beams.Clear();
        _log.Clear();
        _detonationsLogged.Clear();
        _damagedSystems.Clear();
        Paused = false;
        IsMissionActive = false;
        EnemySlot = -1;
        PlayerSlot = -1;
        _locked = false;
        AddLog("Select a race:");
        AddLog("1) Federation");
        AddLog("2) Romulan");
        AddLog("3) Klingon");
        AddLog("4) Orion");
        AddLog("5) Independent");
        AddLog("Press 1-5 to start.");
    }

    public uint Seed { get; }

    public bool IsMissionActive { get; private set; }

    public int PlayerSlot { get; private set; } = -1;

    public int EnemySlot { get; private set; } = -1;

    public IReadOnlyList<int> RobotSlots => _robotSlots;

    public GameSnapshot Snapshot { get; private set; } = null!;

    public IReadOnlyList<BeamEffect> Beams => _beams;

    public IReadOnlyList<string> Log => _log;

    public bool Paused { get; set; }

    public ShipSnapshot Player => PlayerSlot >= 0 && Snapshot is not null ? Snapshot.GetShip(PlayerSlot) : default!;

    public ShipSnapshot Enemy
    {
        get
        {
            if (!IsMissionActive || PlayerSlot < 0 || Snapshot is null)
            {
                return default!;
            }

            if (EnemySlot >= 0 && EnemySlot < Snapshot.Ships.Count)
            {
                var enemy = Snapshot.GetShip(EnemySlot);
                if (enemy.InUse && enemy.Status != DtrekPlayerStatus.Dead)
                {
                    return enemy;
                }
            }

            var nearest = FindNearestHostile();
            if (nearest >= 0)
            {
                EnemySlot = nearest;
                return Snapshot.GetShip(EnemySlot);
            }

            return Player;
        }
    }

    public int EnemyRange =>
        IsMissionActive && PlayerSlot >= 0 && EnemySlot >= 0
            ? (int)Math.Round(Navigation.Distance(Player, Enemy))
            : 0;

    /// <summary>Hostile torpedoes currently in flight.</summary>
    public int IncomingTorpedoes => Snapshot.Torpedoes.Count(
        torpedo => torpedo.IsInFlight && torpedo.OwnerSlot != PlayerSlot);

    /// <summary>Torpedoes launched by the player still in flight.</summary>
    public int OutgoingTorpedoes => Snapshot.Torpedoes.Count(
        torpedo => torpedo.IsInFlight && torpedo.OwnerSlot == PlayerSlot);

    public int CommandedCourse => _course;

    public int CommandedSpeed => _speed;

    /// <summary>Selected ordnance class for the player's torpedo tubes.</summary>
    public DtrekWeaponClass SelectedOrdnance => Player.TorpedoClass;

    /// <summary>Full profile for the selected ordnance.</summary>
    public OrdnanceSpec SelectedOrdnanceSpec => Player.Ordnance;

    /// <summary>True when the player has a weapon lock on the hostile ship.</summary>
    public bool Locked => _locked && IsEnemyAlive;

    /// <summary>True while the player is held in a circular orbit.</summary>
    public bool Orbiting => Player.IsOrbiting;

    /// <summary>True when damage control is enabled.</summary>
    public bool RepairMode => Player.RepairMode;

    /// <summary>True when orbiting a friendly fuel depot.</summary>
    public bool AtFuelPlanet => Player.AtFuelPlanet;

    /// <summary>True when orbiting a friendly repair base.</summary>
    public bool AtRepairPlanet => Player.AtRepairPlanet;

    /// <summary>Planet currently being orbited, or null.</summary>
    public PlanetSnapshot? OrbitPlanet => Player.OrbitPlanetSlot >= 0
        ? Snapshot.FindPlanet(Player.OrbitPlanetSlot)
        : null;

    /// <summary>Nearest planet to the player, or null when the map is empty.</summary>
    public PlanetSnapshot? NearestPlanet => Player.NearestPlanetSlot >= 0
        ? Snapshot.FindPlanet(Player.NearestPlanetSlot)
        : null;

    public int NearestPlanetDistance => Player.NearestPlanetDistance;

    public int PlanetCount => Snapshot.PlanetCount;

    public bool IsPlayerAlive => Player.IsAlive;

    public bool IsEnemyAlive => Enemy.IsAlive;

    public bool IsPlayerDestroyed =>
        Player.Status is DtrekPlayerStatus.Exploding or DtrekPlayerStatus.Dead;

    public bool IsEnemyDestroyed =>
        Enemy.Status is DtrekPlayerStatus.Exploding or DtrekPlayerStatus.Dead;

    /// <summary>Applies one 100 ms update of input, AI, and simulation.</summary>
    public void Update(InputState input)
    {
        if (!IsMissionActive || Paused || Snapshot is null)
        {
            return;
        }

        if (IsPlayerAlive)
        {
            if (input.TurnLeft)
            {
                _course = (_course + 254) & 255;
            }

            if (input.TurnRight)
            {
                _course = (_course + 2) & 255;
            }

            if (_speedCooldown > 0)
            {
                _speedCooldown--;
            }
            else if (input.Accelerate && _speed < DtrekConstants.MaxSpeed)
            {
                _speed++;
                _speedCooldown = 2;
            }
            else if (input.Decelerate && _speed > 0)
            {
                _speed--;
                _speedCooldown = 2;
            }

            _engine.SetControls(PlayerSlot, _course, _speed);
        }

        _engine.Step(1);
        RefreshSnapshot();
        if (Snapshot is null)
        {
            return;
        }

        var nearest = FindNearestHostile();
        if (nearest >= 0)
        {
            EnemySlot = nearest;
        }
        TrackSubsystemDamage();
        TrackRobotEvents();
        TrackDetonations();
        PruneBeams();
    }

    private int FindNearestHostile()
    {
        var player = Player;
        if (!player.InUse)
        {
            return -1;
        }

        int bestSlot = -1;
        var bestRange = double.MaxValue;
        foreach (var slot in _robotSlots)
        {
            if (slot < 0 || slot >= Snapshot.Ships.Count)
            {
                continue;
            }

            var ship = Snapshot.GetShip(slot);
            if (!ship.InUse || ship.Status == DtrekPlayerStatus.Dead
                || ship.Status == DtrekPlayerStatus.Exploding)
            {
                continue;
            }

            var range = Navigation.Distance(player, ship);
            if (range < bestRange)
            {
                bestRange = range;
                bestSlot = slot;
            }
        }

        return bestSlot;
    }

    /// <summary>Fires all loaded banks along the ship's current heading.</summary>
    public PhaserResult? FirePlayerPhasers()
    {
        if (Paused)
        {
            return null;
        }

        var player = Player;
        if (!player.IsAlive)
        {
            AddLog("Weapons offline.");
            return null;
        }

        try
        {
            var shot = _engine.FirePhaser(PlayerSlot, player.Course);
            AddBeam(player, shot, fromPlayer: true, aimCourse: player.Course);
            LogPhaserShot("Reliant", shot, outgoing: true);
            RefreshSnapshot();
            return shot;
        }
        catch (DtrekException ex) when (ex.Result == DtrekResult.WeaponNotReady)
        {
            AddLog("Phaser banks still reloading.");
            return null;
        }
    }

    /// <summary>
    /// Launches one torpedo along the ship's heading. Guided classes use the
    /// current weapon lock; mines are placed one mine range ahead.
    /// </summary>
    public TorpedoResult? FirePlayerTorpedo()
    {
        if (Paused)
        {
            return null;
        }

        var player = Player;
        if (!player.IsAlive)
        {
            AddLog("Weapons offline.");
            return null;
        }

        var launch = new TorpedoLaunch(player.Course)
        {
            TargetSlot = player.Ordnance.Tracking && Locked ? EnemySlot : -1,
        };

        var shot = _engine.FireTorpedo(PlayerSlot, launch);
        if (!shot.Fired)
        {
            AddLog(player.TubesLoaded <= 0
                ? "Torpedo tubes still reloading."
                : "Not enough fuel to arm that ordnance.");
            return null;
        }

        var lockNote = shot.Tracking ? $" locked on {Enemy.Name}" : string.Empty;
        AddLog($"Reliant fires {OrdnanceLabel(shot.WeaponClass)} "
            + $"{shot.TorpedoSlot}{lockNote} "
            + $"({shot.TubesLoaded}/{shot.TubesTotal} tubes).");
        RefreshSnapshot();
        return shot;
    }

    /// <summary>Loads one of the five ordnance classes into the torpedo tubes.</summary>
    public void SelectOrdnance(DtrekWeaponClass weaponClass)
    {
        if (!Player.IsAlive)
        {
            AddLog("Weapons offline.");
            return;
        }

        _engine.SetOrdnance(PlayerSlot, weaponClass);
        RefreshSnapshot();
        AddLog($"Loaded {OrdnanceLabel(weaponClass)} ordnance.");
    }

    /// <summary>Toggles the weapon lock used by missiles and plasma torpedoes.</summary>
    public void ToggleLock()
    {
        if (!IsEnemyAlive)
        {
            AddLog("No target to lock.");
            return;
        }

        if (EnemyRange > LockRange)
        {
            AddLog($"No lock: {Enemy.Name} beyond {LockRange} units.");
            return;
        }

        _locked = !_locked;
        AddLog(_locked
            ? $"Weapon lock: {Enemy.Name} at {EnemyRange} units."
            : "Weapon lock released.");
    }

    /// <summary>
    /// Enters orbit around the nearest eligible planet, or leaves the current
    /// orbit. Orbit requires warp speed at or below the original limit.
    /// </summary>
    public void ToggleOrbit()
    {
        if (Paused)
        {
            return;
        }

        if (!Player.IsAlive)
        {
            AddLog("Helm offline.");
            return;
        }

        if (Player.IsOrbiting)
        {
            var leaving = OrbitPlanet?.Name ?? "the planet";
            _engine.LeaveOrbit(PlayerSlot);
            RefreshSnapshot();
            AddLog($"Leaving orbit of {leaving}.");
            return;
        }

        try
        {
            var planetSlot = _engine.Orbit(PlayerSlot);
            _speed = 0;
            _speedCooldown = 0;
            _engine.SetControls(PlayerSlot, _course, 0);
            RefreshSnapshot();
            var planet = Snapshot.FindPlanet(planetSlot);
            AddLog($"Entered orbit of {planet?.Name ?? $"planet {planetSlot}"}.");
        }
        catch (DtrekException ex) when (ex.Result == DtrekResult.InvalidState)
        {
            AddLog(Player.Speed > DtrekConstants.OrbitSpeed
                ? $"Reduce to warp {DtrekConstants.OrbitSpeed} to orbit."
                : $"No planet within {DtrekConstants.EnterOrbitDistance} units.");
        }
    }

    /// <summary>Toggles damage control while stationed at a friendly repair base.</summary>
    public void ToggleRepair()
    {
        if (!Player.IsAlive)
        {
            AddLog("Damage control offline.");
            return;
        }

        if (!Player.IsOrbiting || !Player.AtRepairPlanet)
        {
            AddLog("Repair needs a friendly base in orbit.");
            return;
        }

        var on = !Player.RepairMode;
        _engine.SetRepair(PlayerSlot, on);
        RefreshSnapshot();
        AddLog(on ? "Damage control engaged." : "Damage control disengaged.");
    }

    public static string OrdnanceLabel(DtrekWeaponClass weaponClass) =>
        weaponClass switch
        {
            DtrekWeaponClass.Photon => "photon torpedo",
            DtrekWeaponClass.Missile => "missile",
            DtrekWeaponClass.Plasma => "plasma torpedo",
            DtrekWeaponClass.Chaff => "chaff cloud",
            DtrekWeaponClass.Mine => "mine",
            _ => "ordnance",
        };

    /// <summary>Short HUD label for one damage-control unit.</summary>
    public static string SystemLabel(ShipSystemSnapshot system) =>
        system.Item switch
        {
            DtrekSystemItem.Armor => "Armor",
            DtrekSystemItem.Hull => "Hull",
            DtrekSystemItem.Engine => "Engine",
            DtrekSystemItem.Fuel => "Fuel",
            DtrekSystemItem.BeamNumber => $"Beam {system.Subitem + 1}",
            DtrekSystemItem.TorpedoNumber => $"Tube {system.Subitem + 1}",
            DtrekSystemItem.Dilithium => "Dilithium",
            DtrekSystemItem.Cargo => "Cargo",
            _ => system.Item.ToString(),
        };

    public void ToggleShields()
    {
        if (!Player.IsAlive)
        {
            return;
        }

        var up = !Player.ShieldsUp;
        _engine.SetShields(PlayerSlot, up);
        RefreshSnapshot();
        AddLog(up ? "Screens raised." : "Screens lowered.");
    }

    public void TogglePause()
    {
        Paused = !Paused;
        AddLog(Paused ? "Simulation paused." : "Simulation resumed.");
    }

    public void Restart()
    {
        if (!IsMissionActive)
        {
            return;
        }

        _engine.Reset(Seed);
        _beams.Clear();
        _log.Clear();
        _detonationsLogged.Clear();
        _robotSlots.Clear();
        _locked = false;
        Paused = false;
        InitializeMission(_playerTeam);
    }

    public void StartMission(DtrekTeam team)
    {
        if (IsMissionActive)
        {
            return;
        }

        _playerTeam = team;
        InitializeMission(team);
    }

    public void Dispose()
    {
        _engine.Dispose();
    }

    private void InitializeMission(DtrekTeam team)
    {
        _engine.GenerateGalaxy(Seed);
        var playerName = team switch
        {
            DtrekTeam.Romulan => "Valdore",
            DtrekTeam.Klingon => "K'mpec",
            DtrekTeam.Orion => "Black Widow",
            DtrekTeam.Independent => "Rogue",
            _ => "Reliant",
        };
        PlayerSlot = _engine.Spawn(playerName, team, PlayerStartX, PlayerStartY);
        _robotSlots.Clear();

        var warbird = _engine.SpawnRobot(
            "Warbird",
            DtrekTeam.Romulan,
            EnemyStartX,
            EnemyStartY,
            EnemyRobotConfig);
        _robotSlots.Add(warbird);
        EnemySlot = warbird;

        var klingon = _engine.SpawnRobot(
            "I.K.S. Mauler",
            DtrekTeam.Klingon,
            128000,
            98000,
            KlingonRobotConfig);
        _robotSlots.Add(klingon);

        var orion = _engine.SpawnRobot(
            "O.R. Corsair",
            DtrekTeam.Orion,
            120000,
            108000,
            OrionRobotConfig);
        _robotSlots.Add(orion);

        var praetor = _engine.SpawnRobot(
            "R.S. Praetor",
            DtrekTeam.Romulan,
            132000,
            106000,
            RomulanHeavyConfig);
        _robotSlots.Add(praetor);

        var patrol = _engine.SpawnRobot(
            team == DtrekTeam.Federation ? "U.S.S. Defiant" : "Patrol",
            team == DtrekTeam.Federation ? DtrekTeam.Klingon : DtrekTeam.Federation,
            88000,
            118000,
            PatrolRobotConfig);
        _robotSlots.Add(patrol);

        _course = Navigation.CourseTo(
            PlayerStartX, PlayerStartY, EnemyStartX, EnemyStartY);
        _speed = 0;
        _speedCooldown = 0;
        _lastRobotBeamTick = -1;
        _lastRobotTorpedoTick = -1;
        _damagedSystems.Clear();
        _locked = false;
        _engine.SetControls(PlayerSlot, _course, _speed);

        RefreshSnapshot();
        IsMissionActive = true;
        AddLog($"{TeamLabel(team)} patrol start. {Snapshot.PlanetCount} charted objects. "
            + $"{_robotSlots.Count} hostiles detected.");
    }

    private static string TeamLabel(DtrekTeam team) => team switch
    {
        DtrekTeam.Romulan => "Romulan",
        DtrekTeam.Klingon => "Klingon",
        DtrekTeam.Orion => "Orion",
        DtrekTeam.Independent => "Independent",
        _ => "Federation",
    };

    /// <summary>
    /// Reports each damage-control unit the first time it takes damage, and
    /// forgets units once they are fully repaired so they can report again.
    /// </summary>
    private void TrackSubsystemDamage()
    {
        var player = Player;
        var damaged = new HashSet<(DtrekSystemItem Item, int Subitem)>();

        foreach (var system in player.Systems)
        {
            if (!system.IsDamaged)
            {
                continue;
            }

            var key = (system.Item, system.Subitem);
            damaged.Add(key);
            if (_damagedSystems.Add(key))
            {
                AddLog(system.Size == 0
                    ? $"{SystemLabel(system)} destroyed."
                    : $"{SystemLabel(system)} damaged "
                        + $"({system.Size}/{system.SizeMaximum}).");
            }
        }

        _damagedSystems.IntersectWith(damaged);
    }

    /// <summary>
    /// Turns native robot telemetry into the beams, log lines, and audio the
    /// old presentation-layer pursuer used to produce. The core robot owns all
    /// targeting and firing; the app only renders what happened.
    /// </summary>
    private void TrackRobotEvents()
    {
        var robot = Enemy;
        if (!robot.IsRobot)
        {
            return;
        }

        if (robot.RobotLastTorpedoTick > _lastRobotTorpedoTick)
        {
            _lastRobotTorpedoTick = robot.RobotLastTorpedoTick;
            var torpedo = robot.RobotLastTorpedoSlot >= 0
                ? Snapshot.Torpedoes.FirstOrDefault(
                    candidate => candidate.Slot == robot.RobotLastTorpedoSlot)
                : null;
            var label = torpedo is not null
                ? OrdnanceLabel(torpedo.WeaponClass)
                : "torpedo";
            var lockNote = robot.RobotLastTorpedoTargetSlot == PlayerSlot
                ? $" locked on {Player.Name}"
                : string.Empty;

            AddLog($"{robot.Name} launches {label} "
                + $"{robot.RobotLastTorpedoSlot}{lockNote}.");
        }

        if (robot.RobotLastBeamTick > _lastRobotBeamTick)
        {
            _lastRobotBeamTick = robot.RobotLastBeamTick;
            AddRobotBeam(robot);

            if (robot.RobotLastBeamTargetSlot == PlayerSlot)
            {
                AddLog(robot.RobotLastBeamHit
                    ? $"{robot.Name} phasers hit you."
                    : $"{robot.Name} phasers missed.");
            }
            else
            {
                AddLog($"{robot.Name} fires phasers.");
            }
        }
    }

    private void AddRobotBeam(ShipSnapshot robot)
    {
        var targetX = robot.X;
        var targetY = robot.Y;

        if (robot.RobotLastBeamTargetSlot >= 0
            && robot.RobotLastBeamTargetSlot < Snapshot.Ships.Count)
        {
            var target = Snapshot.GetShip(robot.RobotLastBeamTargetSlot);
            targetX = target.X;
            targetY = target.Y;
        }
        else
        {
            var (dx, dy) = Navigation.CourseVector(robot.Course);
            targetX += (int)Math.Round(dx * BeamRange);
            targetY += (int)Math.Round(dy * BeamRange);
        }

        _beams.Add(new BeamEffect(
            robot.X, robot.Y, targetX, targetY,
            robot.RobotLastBeamHit, FromPlayer: false,
            Snapshot.Tick + 3));

        while (_beams.Count > 16)
        {
            _beams.RemoveAt(0);
        }
    }

    private void AddBeam(
        ShipSnapshot shooter,
        PhaserResult shot,
        bool fromPlayer,
        int aimCourse)
    {
        var targetX = shooter.X;
        var targetY = shooter.Y;

        if (shot.Hit && shot.TargetIsTorpedo && shot.TorpedoSlot >= 0)
        {
            var torpedo = Snapshot.Torpedoes.FirstOrDefault(
                candidate => candidate.Slot == shot.TorpedoSlot);
            if (torpedo is not null)
            {
                targetX = torpedo.X;
                targetY = torpedo.Y;
            }
        }
        else if (shot.Hit && shot.TargetSlot >= 0)
        {
            var target = Snapshot.GetShip(shot.TargetSlot);
            targetX = target.X;
            targetY = target.Y;
        }
        else
        {
            var (dx, dy) = Navigation.CourseVector(aimCourse);
            targetX += (int)Math.Round(dx * BeamRange);
            targetY += (int)Math.Round(dy * BeamRange);
        }

        _beams.Add(new BeamEffect(
            shooter.X, shooter.Y, targetX, targetY,
            shot.Hit, fromPlayer, Snapshot.Tick + 3));

        while (_beams.Count > 16)
        {
            _beams.RemoveAt(0);
        }
    }

    private void LogPhaserShot(string shooter, PhaserResult shot, bool outgoing)
    {
        if (!shot.Hit)
        {
            AddLog($"{shooter} phasers missed.");
            return;
        }

        if (shot.TargetIsTorpedo)
        {
            if (shot.TorpedoDestroyed)
            {
                _detonationsLogged.Add(shot.TorpedoSlot);
                AddLog($"{shooter} point defense destroyed torpedo "
                    + $"{shot.TorpedoSlot}.");
            }
            else
            {
                AddLog($"{shooter} point defense hit torpedo "
                    + $"{shot.TorpedoSlot} for {shot.TorpedoDamage}.");
            }

            return;
        }

        var target = Snapshot.GetShip(shot.TargetSlot);
        var absorption = shot.ShieldAbsorbed > 0
            ? $"{shot.Shield} screen absorbed {shot.ShieldAbsorbed}"
            : $"hull took {shot.HullDamage}";

        AddLog(outgoing
            ? $"{shooter} hit {target.Name}: {shot.Damage} damage, {absorption}."
            : $"{shooter} hit you: {shot.Damage} damage, {absorption}.");

        if (shot.TargetDestroyed)
        {
            AddLog(outgoing
                ? $"{target.Name} destroyed."
                : "Reliant destroyed.");
        }
    }

    private void RefreshSnapshot()
    {
        try
        {
            Snapshot = _engine.CaptureSnapshot();
        }
        catch
        {
            Snapshot = null!;
        }
    }

    /// <summary>
    /// Logs proximity-fuse detonations once each, ignoring blasts already
    /// reported by a point-defense interception.
    /// </summary>
    private void TrackDetonations()
    {
        var detonating = new HashSet<int>();

        foreach (var torpedo in Snapshot.Torpedoes)
        {
            if (!torpedo.IsDetonating)
            {
                continue;
            }

            detonating.Add(torpedo.Slot);
            if (_detonationsLogged.Add(torpedo.Slot))
            {
                AddLog($"{Capitalize(OrdnanceLabel(torpedo.WeaponClass))} "
                    + $"{torpedo.Slot} detonated "
                    + $"at ({torpedo.X}, {torpedo.Y}).");
            }
        }

        _detonationsLogged.IntersectWith(detonating);
    }

    private void PruneBeams()
    {
        _beams.RemoveAll(beam => beam.ExpiresAtTick < Snapshot.Tick);
    }

    private static string Capitalize(string text)
        => text.Length == 0
            ? text
            : char.ToUpperInvariant(text[0]) + text[1..];

    private void AddLog(string message)
    {
        var tick = Snapshot?.Tick ?? 0;
        _log.Add($"{tick / 10.0,5:0.0}s  {message}");
        if (_log.Count > MaxLogEntries)
        {
            _log.RemoveAt(0);
        }
    }
}
