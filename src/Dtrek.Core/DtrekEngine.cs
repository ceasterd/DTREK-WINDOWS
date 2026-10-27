using System.Runtime.InteropServices;

namespace Dtrek.Core;

/// <summary>
/// Managed façade over one native DTREK simulation engine.
/// </summary>
/// <remarks>
/// Instances are not thread-safe. Use one engine per thread, and make all
/// calls from the same thread that created it.
/// </remarks>
public sealed class DtrekEngine : IDisposable
{
    public const uint SupportedAbiVersion = 6;

    private IntPtr _handle;
    private bool _disposed;

    public DtrekEngine(uint seed = 1989)
    {
        var abi = NativeMethods.dtrek_abi_version();
        if (abi != SupportedAbiVersion)
        {
            throw new InvalidOperationException(
                $"dtrek_core ABI {abi} does not match managed ABI "
                + $"{SupportedAbiVersion}. Rebuild the native core.");
        }

        _handle = NativeMethods.dtrek_create(seed);
        if (_handle == IntPtr.Zero)
        {
            throw new DtrekException(
                DtrekResult.OutOfMemory,
                "Failed to allocate a native DTREK engine.");
        }
    }

    ~DtrekEngine()
    {
        if (_handle != IntPtr.Zero)
        {
            NativeMethods.dtrek_destroy(_handle);
            _handle = IntPtr.Zero;
        }
    }

    public static uint NativeAbiVersion => NativeMethods.dtrek_abi_version();

    public static string NativeVersion =>
        Marshal.PtrToStringAnsi(NativeMethods.dtrek_version_string())
        ?? "unknown";

    /// <summary>Clears all ships and reseeds the deterministic generator.</summary>
    public void Reset(uint seed = 1989)
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_reset(_handle, seed), nameof(Reset));
    }

    /// <summary>
    /// Places the built-in Reliant-class trainer in a free slot and returns
    /// the slot number.
    /// </summary>
    public int Spawn(string name, DtrekTeam team, int x, int y)
    {
        ThrowIfDisposed();
        if (string.IsNullOrWhiteSpace(name))
        {
            name = "Player";
        }

        var slot = NativeMethods.dtrek_spawn(
            _handle, name, (int)team, x, y);

        if (slot < 0)
        {
            throw DtrekException.FromCode(slot, nameof(Spawn));
        }

        return slot;
    }

    /// <summary>
    /// Spawns a Reliant-class trainer controlled by the native robot AI.
    /// <paramref name="config"/> may be null for the original automatic
    /// defaults. Returns the slot number.
    /// </summary>
    public int SpawnRobot(
        string? name,
        DtrekTeam team,
        int x,
        int y,
        DtrekRobotConfig? config = null)
    {
        ThrowIfDisposed();
        if (string.IsNullOrWhiteSpace(name))
        {
            name = "Robot";
        }

        if (team is < DtrekTeam.Independent or > DtrekTeam.Orion)
        {
            throw new ArgumentOutOfRangeException(nameof(team), team, "Unknown team.");
        }

        CheckGalaxyPoint(x, y);
        var native = BuildRobotConfig(config);

        var slot = NativeMethods.dtrek_spawn_robot(
            _handle, name, (int)team, x, y, in native);

        if (slot < 0)
        {
            throw DtrekException.FromCode(slot, nameof(SpawnRobot));
        }

        return slot;
    }

    /// <summary>
    /// Retunes an existing robot. Null selects the original automatic
    /// defaults, exactly as a freshly spawned robot.
    /// </summary>
    public void SetRobot(int shipSlot, DtrekRobotConfig? config = null)
    {
        ThrowIfDisposed();
        var native = BuildRobotConfig(config);
        DtrekException.Check(
            NativeMethods.dtrek_set_robot(_handle, shipSlot, in native),
            nameof(SetRobot));
    }

    /// <summary>Removes a robot and any torpedoes it still has in flight.</summary>
    public void ClearRobot(int shipSlot)
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_clear_robot(_handle, shipSlot),
            nameof(ClearRobot));
    }

    /// <summary>Sets the desired heading (0..255) and warp factor (0..12).</summary>
    public void SetControls(int shipSlot, int desiredCourse, int desiredSpeed)
    {
        ThrowIfDisposed();
        if (desiredCourse is < 0 or > 255)
        {
            throw new ArgumentOutOfRangeException(
                nameof(desiredCourse), desiredCourse,
                "Course must be between 0 and 255.");
        }

        if (desiredSpeed is < 0 or > DtrekConstants.MaxSpeed)
        {
            throw new ArgumentOutOfRangeException(
                nameof(desiredSpeed), desiredSpeed,
                $"Speed must be between 0 and {DtrekConstants.MaxSpeed}.");
        }

        DtrekException.Check(
            NativeMethods.dtrek_set_controls(
                _handle, shipSlot, desiredCourse, desiredSpeed),
            nameof(SetControls));
    }

    public void SetShields(int shipSlot, bool up)
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_set_shields(_handle, shipSlot, up ? 1 : 0),
            nameof(SetShields));
    }

    /// <summary>Advances the simulation by whole 100 ms updates.</summary>
    public void Step(int ticks = 1)
    {
        ThrowIfDisposed();
        if (ticks < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(ticks), ticks, "Tick count cannot be negative.");
        }

        if (ticks == 0)
        {
            return;
        }

        DtrekException.Check(
            NativeMethods.dtrek_step(_handle, ticks), nameof(Step));
    }

    /// <summary>
    /// Fires every loaded phaser bank along <paramref name="aimCourse"/>.
    /// A volley with no target still returns a result with <see cref="PhaserResult.Hit"/>
    /// set to false.
    /// </summary>
    public PhaserResult FirePhaser(int shipSlot, int aimCourse)
    {
        ThrowIfDisposed();
        if (aimCourse is < 0 or > 255)
        {
            throw new ArgumentOutOfRangeException(
                nameof(aimCourse), aimCourse,
                "Course must be between 0 and 255.");
        }

        var result = NativeMethods.dtrek_fire_phaser(
            _handle, shipSlot, aimCourse, out var native);

        if (result < 0)
        {
            throw DtrekException.FromCode(result, nameof(FirePhaser));
        }

        return new PhaserResult(native);
    }

    /// <summary>
    /// Selects one of the five ordnance classes for a ship: photon, missile,
    /// plasma, chaff, or mine.
    /// </summary>
    public void SetOrdnance(int shipSlot, DtrekWeaponClass weaponClass)
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_set_ordnance(
                _handle, shipSlot, (int)weaponClass),
            nameof(SetOrdnance));
    }

    /// <summary>Copies the built-in profile for one ordnance class.</summary>
    public OrdnanceSpec GetOrdnanceSpec(DtrekWeaponClass weaponClass)
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_ordnance_spec(
                _handle, (int)weaponClass, out var native),
            nameof(GetOrdnanceSpec));

        return OrdnanceSpec.FromNative(native);
    }

    /// <summary>
    /// Launches one torpedo along <paramref name="course"/>.
    /// Returns a result with <see cref="TorpedoResult.Fired"/> set to false
    /// when a tube is reloading or the ship cannot pay the fuel cost.
    /// </summary>
    public TorpedoResult FireTorpedo(int shipSlot, int course)
        => FireTorpedo(shipSlot, new TorpedoLaunch(course));

    /// <summary>
    /// Launches one torpedo using a full launch request. Guided classes home
    /// on <see cref="TorpedoLaunch.TargetSlot"/>; mines use the aim point.
    /// </summary>
    public TorpedoResult FireTorpedo(int shipSlot, TorpedoLaunch launch)
    {
        ThrowIfDisposed();
        if (launch.Course is < 0 or > 255)
        {
            throw new ArgumentOutOfRangeException(
                nameof(launch), launch.Course,
                "Course must be between 0 and 255.");
        }

        var nativeLaunch = new NativeTorpedoLaunch
        {
            Course = launch.Course,
            TargetSlot = launch.TargetSlot,
            AimX = launch.AimX,
            AimY = launch.AimY,
            WeaponClass = launch.WeaponClass is { } selected
                ? (int)selected
                : -1,
        };

        var result = NativeMethods.dtrek_fire_torpedo(
            _handle, shipSlot, in nativeLaunch, out var native);

        if (result < 0)
        {
            if (result == (int)DtrekResult.WeaponNotReady)
            {
                return new TorpedoResult(native);
            }

            throw DtrekException.FromCode(result, nameof(FireTorpedo));
        }

        return new TorpedoResult(native);
    }

    /// <summary>Copies the complete simulation state.</summary>
    public GameSnapshot CaptureSnapshot()
    {
        ThrowIfDisposed();

        var native = new NativeGameSnapshot
        {
            Ships = new NativeShipSnapshot[DtrekConstants.MaxPlayers],
            Torpedoes =
                new NativeTorpedoSnapshot[DtrekConstants.MaxTorpedoes],
            Planets = new NativePlanetSnapshot[DtrekConstants.MaxPlanets],
        };

        DtrekException.Check(
            NativeMethods.dtrek_snapshot(_handle, out native),
            nameof(CaptureSnapshot));

        return GameSnapshot.FromNative(native);
    }

    /// <summary>Returns an lrand48-compatible value in [0, limit).</summary>
    public int NextRandom(int limit)
    {
        ThrowIfDisposed();
        if (limit <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(limit), limit, "Limit must be positive.");
        }

        var value = NativeMethods.dtrek_random_below(_handle, limit);
        if (value < 0)
        {
            throw DtrekException.FromCode(value, nameof(NextRandom));
        }

        return value;
    }

    /// <summary>
    /// Generates the authentic 80-object galaxy for <paramref name="seed"/>.
    /// Galaxy randomness is independent of combat randomness.
    /// </summary>
    public void GenerateGalaxy(uint seed = 1989)
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_generate_galaxy(_handle, seed),
            nameof(GenerateGalaxy));
    }

    /// <summary>Removes every planet and spatial feature from the map.</summary>
    public void ClearPlanets()
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_clear_planets(_handle), nameof(ClearPlanets));
    }

    /// <summary>
    /// Writes one planet into a fixed slot. <paramref name="flags"/> should
    /// include exactly one team bit; <paramref name="owner"/> must agree with
    /// it. The low owner bits are rewritten from <paramref name="owner"/>.
    /// </summary>
    public void PlacePlanet(
        int planetSlot,
        DtrekPlanetFlags flags,
        DtrekTeam owner,
        int armies,
        int x,
        int y,
        string? name = null)
    {
        ThrowIfDisposed();
        if (planetSlot is < 0 or >= DtrekConstants.MaxPlanets)
        {
            throw new ArgumentOutOfRangeException(
                nameof(planetSlot), planetSlot,
                $"Planet slot must be between 0 and {DtrekConstants.MaxPlanets - 1}.");
        }

        if (owner is < DtrekTeam.Independent or > DtrekTeam.Orion)
        {
            throw new ArgumentOutOfRangeException(
                nameof(owner), owner, "Unknown team.");
        }

        if (armies < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(armies), armies, "Armies cannot be negative.");
        }

        if (x is < 0 or > DtrekConstants.GalaxyWidth)
        {
            throw new ArgumentOutOfRangeException(
                nameof(x), x, "X must be inside the galaxy.");
        }

        if (y is < 0 or > DtrekConstants.GalaxyWidth)
        {
            throw new ArgumentOutOfRangeException(
                nameof(y), y, "Y must be inside the galaxy.");
        }

        DtrekException.Check(
            NativeMethods.dtrek_place_planet(
                _handle, planetSlot, (int)flags, (int)owner, armies, x, y,
                name),
            nameof(PlacePlanet));
    }

    /// <summary>
    /// Enters orbit around <paramref name="planetSlot"/>, or the nearest
    /// eligible planet when it is -1. Returns the planet slot entered.
    /// Requires warp speed at or below <see cref="DtrekConstants.OrbitSpeed"/>
    /// and range within <see cref="DtrekConstants.EnterOrbitDistance"/>.
    /// </summary>
    public int Orbit(int shipSlot, int planetSlot = -1)
    {
        ThrowIfDisposed();
        if (planetSlot is < -1 or >= DtrekConstants.MaxPlanets)
        {
            throw new ArgumentOutOfRangeException(
                nameof(planetSlot), planetSlot,
                "Planet slot must be -1 or a valid planet slot.");
        }

        var result = NativeMethods.dtrek_orbit(_handle, shipSlot, planetSlot);
        if (result < 0)
        {
            throw DtrekException.FromCode(result, nameof(Orbit));
        }

        return result;
    }

    /// <summary>Releases a ship from orbit and stops planet services.</summary>
    public void LeaveOrbit(int shipSlot)
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_leave_orbit(_handle, shipSlot),
            nameof(LeaveOrbit));
    }

    /// <summary>Turns damage control (repair and rearm) on or off.</summary>
    public void SetRepair(int shipSlot, bool on)
    {
        ThrowIfDisposed();
        DtrekException.Check(
            NativeMethods.dtrek_set_repair(_handle, shipSlot, on ? 1 : 0),
            nameof(SetRepair));
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        var handle = _handle;
        _handle = IntPtr.Zero;

        if (handle != IntPtr.Zero)
        {
            NativeMethods.dtrek_destroy(handle);
        }

        GC.SuppressFinalize(this);
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
    }

    private static void CheckGalaxyPoint(int x, int y)
    {
        if (x is < 0 or > DtrekConstants.GalaxyWidth)
        {
            throw new ArgumentOutOfRangeException(
                nameof(x), x, "X must be inside the galaxy.");
        }

        if (y is < 0 or > DtrekConstants.GalaxyWidth)
        {
            throw new ArgumentOutOfRangeException(
                nameof(y), y, "Y must be inside the galaxy.");
        }
    }

    private static NativeRobotConfig BuildRobotConfig(DtrekRobotConfig? config)
    {
        if (config is null)
        {
            return NativeRobotConfig.Automatic;
        }

        var value = config.Value;
        if (value.Offense is < 0 or > 100)
        {
            throw new ArgumentOutOfRangeException(
                nameof(config), value.Offense,
                "Robot offense must be between 0 and 100.");
        }

        if (value.Impatience is < 0 or > 100)
        {
            throw new ArgumentOutOfRangeException(
                nameof(config), value.Impatience,
                "Robot impatience must be between 0 and 100.");
        }

        if (value.AttackSpeed is < 0 or > DtrekConstants.MaxSpeed)
        {
            throw new ArgumentOutOfRangeException(
                nameof(config), value.AttackSpeed,
                $"Robot attack speed must be between 0 and {DtrekConstants.MaxSpeed}.");
        }

        if ((value.Flags
                & ~(DtrekRobotFlags.Orbiter | DtrekRobotFlags.Starbase)) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(config), value.Flags, "Unknown robot flags.");
        }

        if (value.PatrolPlanet is { } planet
            && planet is < 0 or >= DtrekConstants.MaxPlanets)
        {
            throw new ArgumentOutOfRangeException(
                nameof(config), planet,
                $"Patrol planet must be between 0 and {DtrekConstants.MaxPlanets - 1}.");
        }

        return value.ToNative();
    }
}
