using System.Reflection;
using System.Runtime.InteropServices;

namespace Dtrek.Core;

internal static class NativeMethods
{
    internal const string LibraryName = "dtrek_core";

#pragma warning disable CA1401 // P/Invoke method is intentionally internal.
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern uint dtrek_abi_version();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern IntPtr dtrek_version_string();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern IntPtr dtrek_create(uint seed);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern void dtrek_destroy(IntPtr engine);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_reset(IntPtr engine, uint seed);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi, ExactSpelling = true)]
    internal static extern int dtrek_spawn(
        IntPtr engine,
        [MarshalAs(UnmanagedType.LPStr)] string name,
        int team,
        int x,
        int y);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_set_controls(
        IntPtr engine,
        int shipSlot,
        int desiredCourse,
        int desiredSpeed);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_set_shields(
        IntPtr engine,
        int shipSlot,
        int up);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_step(IntPtr engine, int ticks);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_fire_phaser(
        IntPtr engine,
        int shipSlot,
        int aimCourse,
        out NativePhaserResult result);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_fire_torpedo(
        IntPtr engine,
        int shipSlot,
        in NativeTorpedoLaunch launch,
        out NativeTorpedoResult result);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_set_ordnance(
        IntPtr engine,
        int shipSlot,
        int weaponClass);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_ordnance_spec(
        IntPtr engine,
        int weaponClass,
        out NativeOrdnanceSpec spec);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_snapshot(
        IntPtr engine,
        out NativeGameSnapshot snapshot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_random_below(
        IntPtr engine,
        int limit);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_generate_galaxy(
        IntPtr engine,
        uint seed);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_clear_planets(IntPtr engine);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi, ExactSpelling = true)]
    internal static extern int dtrek_place_planet(
        IntPtr engine,
        int planetSlot,
        int flags,
        int owner,
        int armies,
        int x,
        int y,
        [MarshalAs(UnmanagedType.LPStr)] string? name);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_orbit(
        IntPtr engine,
        int shipSlot,
        int planetSlot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_leave_orbit(
        IntPtr engine,
        int shipSlot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_set_repair(
        IntPtr engine,
        int shipSlot,
        int on);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Ansi, ExactSpelling = true)]
    internal static extern int dtrek_spawn_robot(
        IntPtr engine,
        [MarshalAs(UnmanagedType.LPStr)] string? name,
        int team,
        int x,
        int y,
        in NativeRobotConfig config);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_set_robot(
        IntPtr engine,
        int shipSlot,
        in NativeRobotConfig config);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    internal static extern int dtrek_clear_robot(
        IntPtr engine,
        int shipSlot);
#pragma warning restore CA1401

    static NativeMethods()
    {
        NativeLibrary.SetDllImportResolver(
            typeof(NativeMethods).Assembly, ResolveNativeLibrary);
    }

    private static IntPtr ResolveNativeLibrary(
        string libraryName,
        Assembly assembly,
        DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, LibraryName, StringComparison.Ordinal))
        {
            return IntPtr.Zero;
        }

        var candidates = new[]
        {
            Path.Combine(AppContext.BaseDirectory, "dtrek_core.dll"),
            Path.Combine(AppContext.BaseDirectory, "runtimes", "win-x64",
                "native", "dtrek_core.dll")
        };

        foreach (var candidate in candidates)
        {
            if (File.Exists(candidate)
                && NativeLibrary.TryLoad(candidate, out var handle))
            {
                return handle;
            }
        }

        return IntPtr.Zero;
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeRobotConfig
{
    public int Offense;
    public int Impatience;
    public int AttackSpeed;
    public int Flags;
    public int PatrolPlanet;

    /// <summary>Original automatic defaults, as if no config were supplied.</summary>
    internal static NativeRobotConfig Automatic => new() { PatrolPlanet = -1 };
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSystemSnapshot
{
    public int Item;
    public int Subitem;
    public int Size;
    public int SizeMax;
    public int Fraction;
    public int RepairRate;
    public int PlanetRepairRate;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeShipSnapshot
{
    public int InUse;
    public int Slot;
    public int Team;
    public int Status;
    public int ShieldsUp;
    public fixed byte Name[32];

    public int X;
    public int Y;
    public int Course;
    public int DesiredCourse;
    public int Speed;
    public int DesiredSpeed;

    public fixed int Shield[4];
    public fixed int ShieldMax[4];

    public int Hull;
    public int HullMax;
    public int Armor;
    public int ArmorMax;

    public int Fuel;
    public int FuelMax;
    public int EngineHealth;

    public int BeamsLoaded;
    public int BeamsTotal;
    public int BeamsUnloaded;
    public int BeamsReload;

    public int TubesLoaded;
    public int TubesTotal;
    public int TubesUnloaded;
    public int TubesReload;
    public int TorpedoesInFlight;

    public int Kills;

    public int TorpedoClass;
    public NativeOrdnanceSpec Ordnance;

    public int Orbiting;
    public int PlanetSlot;
    public int RepairMode;
    public int NearestPlanet;
    public int NearestPlanetDistance;
    public int AtFuelPlanet;
    public int AtRepairPlanet;

    public int IsRobot;
    public int RobotOffense;
    public int RobotImpatience;
    public int RobotAttackSpeed;
    public int RobotFlags;
    public int RobotState;
    public int RobotTarget;
    public int RobotPatrolPlanet;
    public int RobotPatrolFuse;
    public int RobotEvasionFuse;
    public int RobotLastBeamTick;
    public int RobotLastBeamTarget;
    public int RobotLastBeamHit;
    public int RobotLastTorpedoTick;
    public int RobotLastTorpedoSlot;
    public int RobotLastTorpedoTarget;

    public int SystemCount;
    public int SystemSize;

    [MarshalAs(UnmanagedType.ByValArray,
        SizeConst = DtrekConstants.MaxSystems)]
    public NativeSystemSnapshot[] Systems;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeOrdnanceSpec
{
    public int WeaponClass;
    public int Speed;
    public int Damage;
    public int Duration;
    public int Reload;
    public int Decay;
    public int FuelCost;
    public int Arm;
    public int Tracking;
    public fixed byte Name[DtrekConstants.OrdnanceNameLength];
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeTorpedoLaunch
{
    public int Course;
    public int TargetSlot;
    public int AimX;
    public int AimY;
    public int WeaponClass;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeTorpedoSnapshot
{
    public int InUse;
    public int Slot;
    public int OwnerSlot;
    public int Status;
    public int Team;
    public int X;
    public int Y;
    public int Course;
    public int Speed;
    public int Damage;
    public int Fuse;
    public int Arm;
    public int WeaponClass;
    public int TargetSlot;

    public int BaseDamage;
    public int Decay;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativePlanetSnapshot
{
    public int InUse;
    public int Slot;
    public int Flags;
    public int Owner;
    public int Armies;
    public int X;
    public int Y;
    public int Dx;
    public int Dy;
    public fixed byte Name[DtrekConstants.PlanetNameLength];
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeGameSnapshot
{
    public int Tick;
    public int RngStateLo;
    public int RngStateHi;
    public int ShipCount;
    public int ActiveCount;
    public int TorpedoCount;
    public int PlanetCount;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = DtrekConstants.MaxPlayers)]
    public NativeShipSnapshot[] Ships;

    [MarshalAs(UnmanagedType.ByValArray,
        SizeConst = DtrekConstants.MaxTorpedoes)]
    public NativeTorpedoSnapshot[] Torpedoes;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = DtrekConstants.MaxPlanets)]
    public NativePlanetSnapshot[] Planets;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativePhaserResult
{
    public int Fired;
    public int Hit;
    public int TargetSlot;
    public int TargetTeam;
    public int Damage;
    public int ShieldIndex;
    public int ShieldAbsorbed;
    public int ArmorAbsorbed;
    public int HullDamage;
    public int Range;
    public int BeamsFired;
    public int FuelCost;
    public int TargetDestroyed;

    public int TargetIsTorpedo;
    public int TorpedoSlot;
    public int TorpedoTeam;
    public int TorpedoDamage;
    public int TorpedoDestroyed;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeTorpedoResult
{
    public int Fired;
    public int TorpedoSlot;
    public int OwnerSlot;
    public int WeaponClass;
    public int TargetSlot;
    public int Tracking;
    public int Arm;
    public int Damage;
    public int Speed;
    public int Fuse;
    public int FuelCost;
    public int TubesLoaded;
    public int TubesTotal;
}
