namespace Dtrek.Core;

/// <summary>
/// Constants shared with the native core ABI.
/// </summary>
public static class DtrekConstants
{
    public const int GalaxyWidth = 200000;
    public const int Warp1 = 25;
    public const int UpdatesPerSecond = 10;
    public const int MaxPlayers = 32;
    public const int MaxBeams = 15;
    public const int MaxTorpedoesPerShip = 24;
    public const int MaxTorpedoes = MaxPlayers * MaxTorpedoesPerShip;
    public const int TorpedoExplodeDistance = 400;
    public const int TorpedoDetonateDistance = 800;
    public const int MaxSpeed = 12;
    public const int TeamCount = 5;
    public const int ShieldCount = 4;
    public const int MaxSystems = 50;
    public const int RepairFuse = 30;
    public const int ShipNameLength = 32;
    public const int OrdnanceNameLength = 32;
    public const int OrdnanceCount = 5;

    public const int MaxPlanets = 80;
    public const int PlanetNameLength = 32;
    public const int OrbitDistance = 800;
    public const int EnterOrbitDistance = 900;
    public const int OrbitSpeed = 2;
    public const int NebulaRadius = GalaxyWidth / 24;
    public const int InterplanetaryDistance = GalaxyWidth / 12;

    public static readonly TimeSpan UpdateInterval =
        TimeSpan.FromMilliseconds(1000.0 / UpdatesPerSecond);
}

/// <summary>Player status values, matching player.h in the original game.</summary>
public enum DtrekPlayerStatus
{
    Free = 0,
    Outfit = 1,
    Alive = 2,
    Exploding = 3,
    Dead = 4,
    Immune = 5,
}

/// <summary>Team identifiers, matching defs.h in the original game.</summary>
public enum DtrekTeam
{
    Independent = 0,
    Federation = 1,
    Romulan = 2,
    Klingon = 3,
    Orion = 4,
}

/// <summary>Shield facing indices, matching defs.h in the original game.</summary>
public enum DtrekShield
{
    Front = 0,
    Right = 1,
    Rear = 2,
    Left = 3,
}

/// <summary>
/// Damageable system identifiers, matching system.h.  The numbers are the
/// original ship builder's item numbers; 24 is the builder title bar and is
/// intentionally absent.
/// </summary>
public enum DtrekSystemItem
{
    TorpedoNumber = 0,
    TorpedoSpeed = 1,
    TorpedoDamage = 2,
    TorpedoDuration = 3,
    TorpedoReload = 4,
    BeamNumber = 5,
    BeamRange = 6,
    BeamDamage = 7,
    BeamAccuracy = 8,
    BeamReload = 9,
    TractorBeam = 10,
    Engine = 11,
    MaxSpeed = 12,
    WarpCost = 13,
    Maneuver = 14,
    Acceleration = 15,
    Deceleration = 16,
    Dilithium = 17,
    DamageControl = 18,
    Cargo = 19,
    FrontShield = 20,
    Armor = 21,
    Hull = 22,
    Fuel = 23,
    SideShield = 25,
    RearShield = 26,
}

/// <summary>
/// Planet feature bits, matching planet.h in the original game. The low five
/// bits are the owner mask, so a planet has exactly one team bit.
/// </summary>
[Flags]
public enum DtrekPlanetFlags
{
    None = 0,
    Independent = 0x0001,
    Federation = 0x0002,
    Romulan = 0x0004,
    Klingon = 0x0008,
    Orion = 0x0010,
    Repair = 0x0020,
    Fuel = 0x0040,
    Redraw = 0x0080,
    Home = 0x0100,
    Coup = 0x0200,
    BlackHole = 0x0400,
    Pulsar = 0x0800,
    Barren = 0x1000,
    Wanderer = 0x2000,
    Nebula = 0x4000,
    TemporaryBarren = 0x8000,
}

/// <summary>Torpedo status values, matching torp.h in the original game.</summary>
public enum TorpedoStatus
{
    Free = 0,
    Move = 1,
    Explode = 2,
    Off = 3,
    Straight = 4,
    Track = 5,
}

/// <summary>Weapon classes, matching weapon.h in the original game.</summary>
public enum DtrekWeaponClass
{
    Photon = 0,
    Missile = 1,
    Plasma = 2,
    Chaff = 3,
    Mine = 4,
    Phaser = 5,
    Disrupter = 6,
    Tachyon = 7,
    Fusion = 8,
    Planet = 9,
    Destruct = 10,
}

/// <summary>
/// Robot roles, matching the -o orbiter and starbase flags in robot.c.
/// A starbase robot is held in orbit and never manoeuvres.
/// </summary>
[Flags]
public enum DtrekRobotFlags
{
    None = 0,
    Orbiter = 0x0001,
    Starbase = 0x0002,
}

/// <summary>
/// Observable robot behaviour, derived from the PATROL_* states in rmove.c.
/// </summary>
public enum DtrekRobotState
{
    Idle = 0,
    Patrol = 1,
    Attack = 2,
    Evade = 3,
    Repair = 4,
    SeekRepair = 5,
    Orbit = 6,
}

/// <summary>
/// Optional robot tuning. Zero offense, impatience, or attack speed selects
/// the original team and ordnance defaults. A null
/// <see cref="PatrolPlanet"/> lets the robot choose a patrol planet.
/// </summary>
public readonly record struct DtrekRobotConfig
{
    /// <summary>Aggressiveness, 1..100, or 0 for automatic.</summary>
    public int Offense { get; init; }

    /// <summary>Impatience, 1..100, or 0 for automatic.</summary>
    public int Impatience { get; init; }

    /// <summary>Attack warp factor, 0..12, or 0 for the team default.</summary>
    public int AttackSpeed { get; init; }

    public DtrekRobotFlags Flags { get; init; }

    /// <summary>Fixed patrol planet slot, or null to choose automatically.</summary>
    public int? PatrolPlanet { get; init; }

    internal NativeRobotConfig ToNative() => new()
    {
        Offense = Offense,
        Impatience = Impatience,
        AttackSpeed = AttackSpeed,
        Flags = (int)Flags,
        PatrolPlanet = PatrolPlanet ?? -1,
    };
}

/// <summary>
/// One ordnance profile. Speed 0 means a stationary mine or, for chaff, the
/// launching ship's current speed.
/// </summary>
public sealed class OrdnanceSpec
{
    internal unsafe OrdnanceSpec(NativeOrdnanceSpec native)
    {
        WeaponClass = (DtrekWeaponClass)native.WeaponClass;
        Speed = native.Speed;
        Damage = native.Damage;
        Duration = native.Duration;
        Reload = native.Reload;
        Decay = native.Decay;
        FuelCost = native.FuelCost;
        Arm = native.Arm;
        Tracking = native.Tracking != 0;

        var name = new byte[DtrekConstants.OrdnanceNameLength];
        for (var i = 0; i < name.Length; i++)
        {
            name[i] = native.Name[i];
            if (name[i] == 0)
            {
                Name = System.Text.Encoding.ASCII.GetString(name, 0, i);
                return;
            }
        }

        Name = System.Text.Encoding.ASCII.GetString(name);
    }

    internal static unsafe OrdnanceSpec FromNative(NativeOrdnanceSpec native)
        => new(native);

    public DtrekWeaponClass WeaponClass { get; }

    public int Speed { get; }

    public int Damage { get; }

    /// <summary>Base fuse in updates, before the random and arming delays.</summary>
    public int Duration { get; }

    public int Reload { get; }

    /// <summary>Damage lost per ten updates, used by plasma torpedoes.</summary>
    public int Decay { get; }

    public int FuelCost { get; }

    /// <summary>Updates before the proximity fuse is armed.</summary>
    public int Arm { get; }

    /// <summary>True when the class can home on a locked target.</summary>
    public bool Tracking { get; }

    public string Name { get; }
}

/// <summary>
/// One torpedo launch request. <see cref="TargetSlot"/> is used only by
/// guided classes; <see cref="AimX"/>/<see cref="AimY"/> are used only by
/// mines. Negative aim coordinates mean "one mine range ahead along course".
/// </summary>
public readonly record struct TorpedoLaunch
{
    public TorpedoLaunch(int course)
    {
        Course = course;
    }

    public int Course { get; init; }

    public int TargetSlot { get; init; } = -1;

    public int AimX { get; init; } = -1;

    public int AimY { get; init; } = -1;

    /// <summary>Overrides the ship's selected class when supplied.</summary>
    public DtrekWeaponClass? WeaponClass { get; init; }
}

/// <summary>Native result codes returned across the C ABI.</summary>
public enum DtrekResult
{
    Ok = 0,
    InvalidArgument = -1,
    Full = -2,
    InvalidShip = -3,
    InvalidState = -4,
    WeaponNotReady = -5,
    OutOfMemory = -6,
}

/// <summary>Thrown when the native core reports an error result.</summary>
public sealed class DtrekException : Exception
{
    public DtrekException(DtrekResult result)
        : base(Describe(result))
    {
        Result = result;
    }

    public DtrekException(DtrekResult result, string message)
        : base(message)
    {
        Result = result;
    }

    public DtrekResult Result { get; }

    internal static void Check(int code, string operation)
    {
        if (code < 0)
        {
            var result = (DtrekResult)code;
            throw new DtrekException(
                result, $"{operation} failed: {Describe(result)} ({code}).");
        }
    }

    internal static DtrekException FromCode(int code, string operation)
    {
        var result = (DtrekResult)code;
        return new DtrekException(
            result, $"{operation} failed: {Describe(result)} ({code}).");
    }

    private static string Describe(DtrekResult result) => result switch
    {
        DtrekResult.Ok => "success",
        DtrekResult.InvalidArgument => "invalid argument",
        DtrekResult.Full => "no free player slots",
        DtrekResult.InvalidShip => "unknown ship slot",
        DtrekResult.InvalidState => "ship cannot perform that action",
        DtrekResult.WeaponNotReady => "weapon is not ready",
        DtrekResult.OutOfMemory => "native allocation failed",
        _ => $"unknown error {(int)result}",
    };
}

/// <summary>
/// One entry in a ship's damage hit table.  Sizes are the original ktons and
/// repair rates are in 1/256 points per thirty-update damage-control pulse.
/// </summary>
public sealed class ShipSystemSnapshot
{
    internal ShipSystemSnapshot(NativeSystemSnapshot native)
    {
        Item = (DtrekSystemItem)native.Item;
        Subitem = native.Subitem;
        Size = native.Size;
        SizeMaximum = native.SizeMax;
        Fraction = native.Fraction;
        RepairRate = native.RepairRate;
        PlanetRepairRate = native.PlanetRepairRate;
    }

    /// <summary>Which ship system this unit represents.</summary>
    public DtrekSystemItem Item { get; }

    /// <summary>Beam bank or torpedo tube number, or zero for single units.</summary>
    public int Subitem { get; }

    /// <summary>Condition in ktons.</summary>
    public int Size { get; }

    /// <summary>Fully repaired size in ktons.</summary>
    public int SizeMaximum { get; }

    /// <summary>Partial repair point accumulator in 1/256 units.</summary>
    public int Fraction { get; }

    /// <summary>Self repair in 1/256 points per thirty-update pulse.</summary>
    public int RepairRate { get; }

    /// <summary>Additional repair at a friendly base, in 1/256 points per pulse.</summary>
    public int PlanetRepairRate { get; }

    /// <summary>True when the unit has lost condition.</summary>
    public bool IsDamaged => Size < SizeMaximum;

    /// <summary>True when damage control can repair this unit without a base.</summary>
    public bool CanSelfRepair => RepairRate > 0;

    /// <summary>Fraction of the unit still working, from 0 to 1.</summary>
    public double Condition => SizeMaximum > 0
        ? (double)Size / SizeMaximum
        : 0.0;

    public override string ToString()
        => Subitem > 0
            ? $"{Item} #{Subitem} {Size}/{SizeMaximum}"
            : $"{Item} {Size}/{SizeMaximum}";
}

/// <summary>Immutable view of one ship at a point in time.</summary>
public sealed class ShipSnapshot
{
    private readonly int[] _shields;
    private readonly int[] _shieldMaximums;
    private readonly ShipSystemSnapshot[] _systems;

    internal ShipSnapshot(NativeShipSnapshot native)
    {
        InUse = native.InUse != 0;
        Slot = native.Slot;
        Team = (DtrekTeam)native.Team;
        Status = (DtrekPlayerStatus)native.Status;
        ShieldsUp = native.ShieldsUp != 0;
        Name = ReadName(native);

        X = native.X;
        Y = native.Y;
        Course = native.Course;
        DesiredCourse = native.DesiredCourse;
        Speed = native.Speed;
        DesiredSpeed = native.DesiredSpeed;

        _shields =
        [
            ReadShield(native, 0),
            ReadShield(native, 1),
            ReadShield(native, 2),
            ReadShield(native, 3),
        ];
        _shieldMaximums =
        [
            ReadShieldMax(native, 0),
            ReadShieldMax(native, 1),
            ReadShieldMax(native, 2),
            ReadShieldMax(native, 3),
        ];

        Hull = native.Hull;
        HullMaximum = native.HullMax;
        Armor = native.Armor;
        ArmorMaximum = native.ArmorMax;

        Fuel = native.Fuel;
        FuelMaximum = native.FuelMax;
        EngineHealth = native.EngineHealth;

        BeamsLoaded = native.BeamsLoaded;
        BeamsTotal = native.BeamsTotal;
        BeamsUnloaded = native.BeamsUnloaded;
        BeamsReload = native.BeamsReload;

        TubesLoaded = native.TubesLoaded;
        TubesTotal = native.TubesTotal;
        TubesUnloaded = native.TubesUnloaded;
        TubesReload = native.TubesReload;
        TorpedoesInFlight = native.TorpedoesInFlight;

        Kills = native.Kills;

        TorpedoClass = (DtrekWeaponClass)native.TorpedoClass;
        Ordnance = OrdnanceSpec.FromNative(native.Ordnance);

        IsOrbiting = native.Orbiting != 0;
        OrbitPlanetSlot = native.PlanetSlot;
        RepairMode = native.RepairMode != 0;
        NearestPlanetSlot = native.NearestPlanet;
        NearestPlanetDistance = native.NearestPlanetDistance;
        AtFuelPlanet = native.AtFuelPlanet != 0;
        AtRepairPlanet = native.AtRepairPlanet != 0;

        IsRobot = native.IsRobot != 0;
        RobotOffense = native.RobotOffense;
        RobotImpatience = native.RobotImpatience;
        RobotAttackSpeed = native.RobotAttackSpeed;
        RobotFlags = (DtrekRobotFlags)native.RobotFlags;
        RobotState = (DtrekRobotState)native.RobotState;
        RobotTargetSlot = native.RobotTarget;
        RobotPatrolPlanetSlot = native.RobotPatrolPlanet;
        RobotPatrolFuse = native.RobotPatrolFuse;
        RobotEvasionFuse = native.RobotEvasionFuse;
        RobotLastBeamTick = native.RobotLastBeamTick;
        RobotLastBeamTargetSlot = native.RobotLastBeamTarget;
        RobotLastBeamHit = native.RobotLastBeamHit != 0;
        RobotLastTorpedoTick = native.RobotLastTorpedoTick;
        RobotLastTorpedoSlot = native.RobotLastTorpedoSlot;
        RobotLastTorpedoTargetSlot = native.RobotLastTorpedoTarget;

        SystemCount = Math.Clamp(native.SystemCount, 0, DtrekConstants.MaxSystems);
        SystemSize = native.SystemSize;
        _systems = new ShipSystemSnapshot[SystemCount];
        for (var i = 0; i < SystemCount; i++)
        {
            _systems[i] = new ShipSystemSnapshot(native.Systems[i]);
        }
    }

    public bool InUse { get; }

    public int Slot { get; }

    public DtrekTeam Team { get; }

    public DtrekPlayerStatus Status { get; }

    public bool ShieldsUp { get; }

    public string Name { get; }

    public int X { get; }

    public int Y { get; }

    /// <summary>Current heading, 0..255. Zero points up, 64 points right.</summary>
    public int Course { get; }

    public int DesiredCourse { get; }

    /// <summary>Current warp factor, 0..12.</summary>
    public int Speed { get; }

    public int DesiredSpeed { get; }

    public IReadOnlyList<int> Shields => _shields;

    public IReadOnlyList<int> ShieldMaximums => _shieldMaximums;

    public int FrontShield => _shields[(int)DtrekShield.Front];

    public int RightShield => _shields[(int)DtrekShield.Right];

    public int RearShield => _shields[(int)DtrekShield.Rear];

    public int LeftShield => _shields[(int)DtrekShield.Left];

    public int Hull { get; }

    public int HullMaximum { get; }

    public int Armor { get; }

    public int ArmorMaximum { get; }

    public int Fuel { get; }

    public int FuelMaximum { get; }

    /// <summary>Engine condition in 1/256 of nominal output.</summary>
    public int EngineHealth { get; }

    public int BeamsLoaded { get; }

    public int BeamsTotal { get; }

    public int BeamsUnloaded { get; }

    public int BeamsReload { get; }

    public int TubesLoaded { get; }

    public int TubesTotal { get; }

    public int TubesUnloaded { get; }

    /// <summary>Updates remaining before the next tube finishes reloading.</summary>
    public int TubesReload { get; }

    /// <summary>Torpedoes this ship currently has in flight.</summary>
    public int TorpedoesInFlight { get; }

    public int Kills { get; }

    /// <summary>Selected ordnance class for this ship's torpedo tubes.</summary>
    public DtrekWeaponClass TorpedoClass { get; }

    /// <summary>Full profile for the selected ordnance class.</summary>
    public OrdnanceSpec Ordnance { get; }

    /// <summary>True while the ship is held in a circular orbit.</summary>
    public bool IsOrbiting { get; }

    /// <summary>Planet slot being orbited, or -1.</summary>
    public int OrbitPlanetSlot { get; }

    /// <summary>True when damage control (repair/rearm) is enabled.</summary>
    public bool RepairMode { get; }

    /// <summary>Nearest planet slot, or -1 when the map is empty.</summary>
    public int NearestPlanetSlot { get; }

    public int NearestPlanetDistance { get; }

    /// <summary>True when orbiting a friendly fuel depot.</summary>
    public bool AtFuelPlanet { get; }

    /// <summary>True when orbiting a friendly repair base.</summary>
    public bool AtRepairPlanet { get; }

    /// <summary>Number of valid entries in the damage hit table.</summary>
    public int SystemCount { get; }

    /// <summary>Current total size of every non-armor, non-hull system.</summary>
    public int SystemSize { get; }

    /// <summary>The ship's damage hit table, in the original order.</summary>
    public IReadOnlyList<ShipSystemSnapshot> Systems => _systems;

    /// <summary>Number of hit-table units below their undamaged size.</summary>
    public int DamagedSystemCount
    {
        get
        {
            var count = 0;
            foreach (var system in _systems)
            {
                if (system.IsDamaged)
                {
                    count++;
                }
            }

            return count;
        }
    }

    /// <summary>
    /// Returns one unit of the hit table, or null when it is not present.
    /// </summary>
    public ShipSystemSnapshot? FindSystem(
        DtrekSystemItem item,
        int subitem = 0)
    {
        foreach (var system in _systems)
        {
            if (system.Item == item && system.Subitem == subitem)
            {
                return system;
            }
        }

        return null;
    }

    /// <summary>True when the ship reports damage control or base repair.</summary>
    public bool HasSystemDamage => DamagedSystemCount > 0;

    /// <summary>True when the native core controls this ship as a robot.</summary>
    public bool IsRobot { get; }

    public int RobotOffense { get; }

    public int RobotImpatience { get; }

    public int RobotAttackSpeed { get; }

    public DtrekRobotFlags RobotFlags { get; }

    public DtrekRobotState RobotState { get; }

    /// <summary>Offensive target slot, or -1.</summary>
    public int RobotTargetSlot { get; }

    /// <summary>Current patrol planet slot, or -1.</summary>
    public int RobotPatrolPlanetSlot { get; }

    public int RobotPatrolFuse { get; }

    public int RobotEvasionFuse { get; }

    /// <summary>Update of the robot's most recent phaser volley, or -1.</summary>
    public int RobotLastBeamTick { get; }

    /// <summary>Target of the most recent phaser volley, or -1.</summary>
    public int RobotLastBeamTargetSlot { get; }

    public bool RobotLastBeamHit { get; }

    /// <summary>Update of the robot's most recent torpedo launch, or -1.</summary>
    public int RobotLastTorpedoTick { get; }

    /// <summary>Global slot of the most recent robot torpedo, or -1.</summary>
    public int RobotLastTorpedoSlot { get; }

    /// <summary>Target of the most recent robot torpedo, or -1.</summary>
    public int RobotLastTorpedoTargetSlot { get; }

    public int GetShield(DtrekShield shield) => _shields[(int)shield];

    public int GetShieldMaximum(DtrekShield shield) => _shieldMaximums[(int)shield];

    public bool IsAlive => InUse && Status == DtrekPlayerStatus.Alive;

    private static unsafe string ReadName(NativeShipSnapshot native)
    {
        var buffer = new byte[DtrekConstants.ShipNameLength];
        for (var i = 0; i < buffer.Length; i++)
        {
            buffer[i] = native.Name[i];
            if (buffer[i] == 0)
            {
                return System.Text.Encoding.ASCII.GetString(buffer, 0, i);
            }
        }

        return System.Text.Encoding.ASCII.GetString(buffer);
    }

    private static unsafe int ReadShield(NativeShipSnapshot native, int index)
        => native.Shield[index];

    private static unsafe int ReadShieldMax(NativeShipSnapshot native, int index)
        => native.ShieldMax[index];
}

/// <summary>Immutable view of one torpedo slot at a point in time.</summary>
public sealed class TorpedoSnapshot
{
    internal TorpedoSnapshot(NativeTorpedoSnapshot native)
    {
        InUse = native.InUse != 0;
        Slot = native.Slot;
        OwnerSlot = native.OwnerSlot;
        Status = (TorpedoStatus)native.Status;
        Team = (DtrekTeam)native.Team;
        X = native.X;
        Y = native.Y;
        Course = native.Course;
        Speed = native.Speed;
        Damage = native.Damage;
        Fuse = native.Fuse;
        Arm = native.Arm;
        WeaponClass = (DtrekWeaponClass)native.WeaponClass;
        TargetSlot = native.TargetSlot;
        BaseDamage = native.BaseDamage;
        Decay = native.Decay;
    }

    public bool InUse { get; }

    /// <summary>Global slot index, 0..767.</summary>
    public int Slot { get; }

    /// <summary>Slot of the ship that launched this torpedo.</summary>
    public int OwnerSlot { get; }

    public TorpedoStatus Status { get; }

    public DtrekTeam Team { get; }

    public int X { get; }

    public int Y { get; }

    /// <summary>Current heading, 0..255. Zero points up, 64 points right.</summary>
    public int Course { get; }

    public int Speed { get; }

    /// <summary>Remaining warhead damage; beam interception reduces this.</summary>
    public int Damage { get; }

    /// <summary>Updates remaining before the torpedo expires or its blast ends.</summary>
    public int Fuse { get; }

    public int Arm { get; }

    public DtrekWeaponClass WeaponClass { get; }

    /// <summary>Tracking target ship slot, or -1 for unguided torpedoes.</summary>
    public int TargetSlot { get; }

    /// <summary>Warhead damage at launch, before decay or interception.</summary>
    public int BaseDamage { get; }

    /// <summary>Damage lost per ten updates, used by plasma torpedoes.</summary>
    public int Decay { get; }

    public bool IsInFlight =>
        InUse && Status is TorpedoStatus.Move
            or TorpedoStatus.Straight
            or TorpedoStatus.Track;

    public bool IsDetonating => InUse && Status == TorpedoStatus.Explode;
}

/// <summary>Immutable view of one planet or spatial feature.</summary>
public sealed class PlanetSnapshot
{
    internal unsafe PlanetSnapshot(NativePlanetSnapshot native)
    {
        InUse = native.InUse != 0;
        Slot = native.Slot;
        Flags = (DtrekPlanetFlags)native.Flags;
        Owner = (DtrekTeam)native.Owner;
        Armies = native.Armies;
        X = native.X;
        Y = native.Y;
        DriftX = native.Dx;
        DriftY = native.Dy;
        Name = ReadName(native);
    }

    public bool InUse { get; }

    /// <summary>Fixed planet slot, 0..79.</summary>
    public int Slot { get; }

    public DtrekPlanetFlags Flags { get; }

    public DtrekTeam Owner { get; }

    public int Armies { get; }

    public int X { get; }

    public int Y { get; }

    /// <summary>Wanderer velocity in galaxy units per update.</summary>
    public int DriftX { get; }

    public int DriftY { get; }

    public string Name { get; }

    public bool IsHomeWorld => (Flags & DtrekPlanetFlags.Home) != 0;

    public bool IsFuelDepot => (Flags & DtrekPlanetFlags.Fuel) != 0;

    public bool IsRepairBase => (Flags & DtrekPlanetFlags.Repair) != 0;

    public bool IsBlackHole => (Flags & DtrekPlanetFlags.BlackHole) != 0;

    public bool IsPulsar => (Flags & DtrekPlanetFlags.Pulsar) != 0;

    public bool IsNebula => (Flags & DtrekPlanetFlags.Nebula) != 0;

    public bool IsWanderer => (Flags & DtrekPlanetFlags.Wanderer) != 0;

    /// <summary>
    /// Independents trade with everyone; otherwise the owner must match.
    /// </summary>
    public bool IsFriendlyTo(DtrekTeam team)
        => Owner == team || Owner == DtrekTeam.Independent;

    private static unsafe string ReadName(NativePlanetSnapshot native)
    {
        var buffer = new byte[DtrekConstants.PlanetNameLength];
        for (var i = 0; i < buffer.Length; i++)
        {
            buffer[i] = native.Name[i];
            if (buffer[i] == 0)
            {
                return System.Text.Encoding.ASCII.GetString(buffer, 0, i);
            }
        }

        return System.Text.Encoding.ASCII.GetString(buffer);
    }
}

/// <summary>Immutable view of the whole simulation at a point in time.</summary>
public sealed class GameSnapshot
{
    private GameSnapshot(NativeGameSnapshot native)
    {
        Tick = native.Tick;
        RngState = ((ulong)(uint)native.RngStateHi << 32)
            | (uint)native.RngStateLo;
        ShipCount = native.ShipCount;
        ActiveCount = native.ActiveCount;
        TorpedoCount = native.TorpedoCount;
        PlanetCount = native.PlanetCount;

        var ships = new ShipSnapshot[DtrekConstants.MaxPlayers];
        for (var i = 0; i < ships.Length; i++)
        {
            ships[i] = new ShipSnapshot(native.Ships[i]);
        }

        Ships = ships;

        var torpedoes = new List<TorpedoSnapshot>(native.TorpedoCount);
        for (var i = 0; i < native.Torpedoes.Length; i++)
        {
            var torpedo = new TorpedoSnapshot(native.Torpedoes[i]);
            if (torpedo.InUse)
            {
                torpedoes.Add(torpedo);
            }
        }

        Torpedoes = torpedoes;

        var planets = new List<PlanetSnapshot>(native.PlanetCount);
        for (var i = 0; i < native.Planets.Length; i++)
        {
            var planet = new PlanetSnapshot(native.Planets[i]);
            if (planet.InUse)
            {
                planets.Add(planet);
            }
        }

        Planets = planets;
    }

    public int Tick { get; }

    public ulong RngState { get; }

    public int ShipCount { get; }

    public int ActiveCount { get; }

    /// <summary>Number of torpedo slots currently in use.</summary>
    public int TorpedoCount { get; }

    /// <summary>Number of planets and spatial features currently in use.</summary>
    public int PlanetCount { get; }

    /// <summary>All 32 slots, including free ones. Index by slot number.</summary>
    public IReadOnlyList<ShipSnapshot> Ships { get; }

    /// <summary>Every torpedo currently in use, in slot order.</summary>
    public IReadOnlyList<TorpedoSnapshot> Torpedoes { get; }

    /// <summary>Every planet currently in use, in slot order.</summary>
    public IReadOnlyList<PlanetSnapshot> Planets { get; }

    public ShipSnapshot GetShip(int slot)
    {
        if (slot < 0 || slot >= Ships.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(slot));
        }

        return Ships[slot];
    }

    /// <summary>Returns a planet by its fixed slot, or null when the slot is empty.</summary>
    public PlanetSnapshot? FindPlanet(int slot)
    {
        foreach (var planet in Planets)
        {
            if (planet.Slot == slot)
            {
                return planet;
            }
        }

        return null;
    }

    internal static GameSnapshot FromNative(NativeGameSnapshot native)
        => new(native);
}

/// <summary>Outcome of one phaser volley.</summary>
public sealed class PhaserResult
{
    internal PhaserResult(NativePhaserResult native)
    {
        Fired = native.Fired != 0;
        Hit = native.Hit != 0;
        TargetSlot = native.TargetSlot;
        TargetTeam = (DtrekTeam)native.TargetTeam;
        Damage = native.Damage;
        ShieldIndex = native.ShieldIndex;
        ShieldAbsorbed = native.ShieldAbsorbed;
        ArmorAbsorbed = native.ArmorAbsorbed;
        HullDamage = native.HullDamage;
        Range = native.Range;
        BeamsFired = native.BeamsFired;
        FuelCost = native.FuelCost;
        TargetDestroyed = native.TargetDestroyed != 0;

        TargetIsTorpedo = native.TargetIsTorpedo != 0;
        TorpedoSlot = native.TorpedoSlot;
        TorpedoTeam = (DtrekTeam)native.TorpedoTeam;
        TorpedoDamage = native.TorpedoDamage;
        TorpedoDestroyed = native.TorpedoDestroyed != 0;
    }

    public bool Fired { get; }

    public bool Hit { get; }

    public int TargetSlot { get; }

    public DtrekTeam TargetTeam { get; }

    public int Damage { get; }

    public int ShieldIndex { get; }

    public DtrekShield Shield => (DtrekShield)ShieldIndex;

    public int ShieldAbsorbed { get; }

    public int ArmorAbsorbed { get; }

    public int HullDamage { get; }

    public int Range { get; }

    public int BeamsFired { get; }

    public int FuelCost { get; }

    public bool TargetDestroyed { get; }

    /// <summary>True when point defense engaged a torpedo instead of a ship.</summary>
    public bool TargetIsTorpedo { get; }

    /// <summary>Global torpedo slot that was engaged, or -1.</summary>
    public int TorpedoSlot { get; }

    public DtrekTeam TorpedoTeam { get; }

    /// <summary>Beam damage applied to the torpedo.</summary>
    public int TorpedoDamage { get; }

    /// <summary>True when the beam consumed the torpedo's remaining warhead.</summary>
    public bool TorpedoDestroyed { get; }
}

/// <summary>Outcome of one torpedo launch.</summary>
public sealed class TorpedoResult
{
    internal TorpedoResult(NativeTorpedoResult native)
    {
        Fired = native.Fired != 0;
        TorpedoSlot = native.TorpedoSlot;
        OwnerSlot = native.OwnerSlot;
        WeaponClass = (DtrekWeaponClass)native.WeaponClass;
        TargetSlot = native.TargetSlot;
        Tracking = native.Tracking != 0;
        Arm = native.Arm;
        Damage = native.Damage;
        Speed = native.Speed;
        Fuse = native.Fuse;
        FuelCost = native.FuelCost;
        TubesLoaded = native.TubesLoaded;
        TubesTotal = native.TubesTotal;
    }

    public bool Fired { get; }

    /// <summary>Global slot of the launched torpedo, or -1.</summary>
    public int TorpedoSlot { get; }

    public int OwnerSlot { get; }

    public DtrekWeaponClass WeaponClass { get; }

    /// <summary>Locked target slot for guided weapons, or -1.</summary>
    public int TargetSlot { get; }

    /// <summary>True when the weapon entered tracking mode.</summary>
    public bool Tracking { get; }

    /// <summary>Updates before the proximity fuse arms.</summary>
    public int Arm { get; }

    public int Damage { get; }

    public int Speed { get; }

    public int Fuse { get; }

    public int FuelCost { get; }

    public int TubesLoaded { get; }

    public int TubesTotal { get; }
}
