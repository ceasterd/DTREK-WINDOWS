using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using Dtrek.Core;

namespace Dtrek.App;

public partial class MainWindow : Window
{
    private static readonly Brush Healthy = MakeBrush(0x66, 0xFF, 0xB2);
    private static readonly Brush Warning = MakeBrush(0xFF, 0xC8, 0x5C);
    private static readonly Brush Critical = MakeBrush(0xFF, 0x6B, 0x6B);
    private static readonly Brush DefaultText = MakeBrush(0xD6, 0xE2, 0xF0);

    private readonly GameSession _game;
    private readonly DispatcherTimer _timer;
    private readonly HashSet<Key> _held = [];

    public MainWindow()
    {
        InitializeComponent();

        _game = new GameSession(seed: 1989);
        _timer = new DispatcherTimer(DispatcherPriority.Render)
        {
            Interval = DtrekConstants.UpdateInterval,
        };
        _timer.Tick += OnTick;
        _timer.Start();

        ControlsText.Text = string.Join(Environment.NewLine, new[]
        {
            "A / Left    turn left",
            "D / Right   turn right",
            "W / Up      increase warp",
            "S / Down    decrease warp",
            "Space       fire phasers",
            "F           fire torpedo",
            "1 - 5       photon / missile /",
            "            plasma / chaff / mine",
            "L           weapon lock",
            "O           orbit / leave orbit",
            "T           damage control",
            "Tab         raise/lower screens",
            "P           pause",
            "R           restart patrol",
        });

        EngineText.Text =
            $"native {DtrekEngine.NativeVersion}  /  ABI {DtrekEngine.NativeAbiVersion}";

        RefreshUi();
        Loaded += (_, _) => Focus();
    }

    private void OnTick(object? sender, EventArgs e)
    {
        if (_game.IsMissionActive && !_game.Paused)
        {
            _game.Update(new InputState(
                TurnLeft: _held.Contains(Key.A) || _held.Contains(Key.Left),
                TurnRight: _held.Contains(Key.D) || _held.Contains(Key.Right),
                Accelerate: _held.Contains(Key.W) || _held.Contains(Key.Up),
                Decelerate: _held.Contains(Key.S) || _held.Contains(Key.Down)));
        }

        RefreshUi();
    }

    private void RefreshUi()
    {
        if (!_game.IsMissionActive)
        {
            StatusText.Text = "SELECT RACE";
            StatusText.Foreground = Warning;
            ClockText.Text = "Press 1-5 to start a patrol";

            HullText.Text = Row("Hull", 0, 1);
            ArmorText.Text = Row("Armor", 0, 1);
            FuelText.Text = Row("Fuel", 0, 1);
            FuelText.Foreground = DefaultText;
            KillsText.Text = "Kills  0";

            DamageSummaryText.Text = "Select your team";
            DamageSummaryText.Foreground = Warning;
            DamageEmptyText.Visibility = Visibility.Collapsed;
            DamagePanel.Children.Clear();

            ScreenText.Text = "Screens  --";
            ScreenText.Foreground = DefaultText;
            SetShield(FrontShieldText, "Fwd", 0, 1);
            SetShield(RightShieldText, "Right", 0, 1);
            SetShield(RearShieldText, "Rear", 0, 1);
            SetShield(LeftShieldText, "Left", 0, 1);

            SpeedText.Text = "Warp     0 / 12";
            CourseText.Text = "Course    0     0°";
            BeamsText.Text = "Phasers --";
            BeamsText.Foreground = DefaultText;
            TubesText.Text = "Torpedo --";
            TubesText.Foreground = DefaultText;
            OrdnanceText.Text = "Load     --";
            OrdnanceText.Foreground = DefaultText;
            LockText.Text = "Lock     --";
            LockText.Foreground = DefaultText;

            OrbitText.Text = "Orbit    none";
            OrbitText.Foreground = DefaultText;
            PlanetText.Text = "Nearest --";
            PlanetText.Foreground = DefaultText;
            ServiceText.Text = "Service none";
            ServiceText.Foreground = DefaultText;
            ThreatText.Text = "Incoming none";
            ThreatText.Foreground = DefaultText;

            TargetNameText.Text = "No contact";
            TargetRangeText.Text = "Range   --";
            TargetStatusText.Text = "Hull    --";
            TargetShieldText.Text = "Screens --";
            TargetAiText.Text = "AI      --";
            TargetNameText.Foreground = DefaultText;
            TargetRangeText.Foreground = DefaultText;
            TargetAiText.Foreground = DefaultText;

            var menuLog = _game.Log;
            MessageText.Text = string.Join(Environment.NewLine, menuLog.TakeLast(9));

            View.ShowGalaxyMap = _showGalaxyMap;
            View.SetScene(
                _game.Snapshot ?? CreateEmptySnapshot(),
                -1,
                -1,
                Array.Empty<BeamEffect>(),
                0,
                _showGalaxyMap ? "GALAXY MAP" : "SELECT RACE",
                _showGalaxyMap
                    ? "G to close"
                    : "1) Federation   2) Romulan   3) Klingon   4) Orion   5) Independent");

            return;
        }

        var snapshot = _game.Snapshot;
        var player = _game.Player;
        var enemy = _game.Enemy;

        var (status, statusBrush) = ResolveStatus();
        StatusText.Text = status;
        StatusText.Foreground = statusBrush;

        var elapsed = TimeSpan.FromSeconds(snapshot.Tick / 10.0);
        ClockText.Text =
            $"T+{elapsed:mm\\:ss}   tick {snapshot.Tick}   seed {_game.Seed}";

        HullText.Text = Row("Hull", player.Hull, player.HullMaximum);
        HullText.Foreground = Health(player.Hull, player.HullMaximum);

        ArmorText.Text = Row("Armor", player.Armor, player.ArmorMaximum);
        FuelText.Text = Row("Fuel", player.Fuel, player.FuelMaximum);
        FuelText.Foreground = player.Fuel <= 0 ? Critical : DefaultText;
        KillsText.Text = $"Kills  {player.Kills}";

        UpdateDamageControl(player);

        ScreenText.Text = $"Screens  {(player.ShieldsUp ? "UP" : "DOWN")}";
        ScreenText.Foreground = player.ShieldsUp ? Healthy : Warning;

        SetShield(FrontShieldText, "Fwd",
            player.FrontShield, player.ShieldMaximums[0]);
        SetShield(RightShieldText, "Right",
            player.RightShield, player.ShieldMaximums[1]);
        SetShield(RearShieldText, "Rear",
            player.RearShield, player.ShieldMaximums[2]);
        SetShield(LeftShieldText, "Left",
            player.LeftShield, player.ShieldMaximums[3]);

        SpeedText.Text =
            $"Warp    {player.Speed} / {DtrekConstants.MaxSpeed}"
            + $"   order {player.DesiredSpeed}";
        var degrees = (int)Math.Round(player.Course * 360.0 / 256.0) % 360;
        CourseText.Text = $"Course  {player.Course,3}   {degrees,3}\u00B0";
        BeamsText.Text = player.BeamsLoaded > 0
            ? $"Phasers {player.BeamsLoaded}/{player.BeamsTotal} ready"
            : $"Phasers reload {player.BeamsReload / 10.0:0.0}s";
        BeamsText.Foreground = player.BeamsLoaded > 0 ? Healthy : Warning;

        TubesText.Text = player.TubesLoaded > 0
            ? $"Torpedo {player.TubesLoaded}/{player.TubesTotal} loaded"
            : $"Torpedo reload {player.TubesReload / 10.0:0.0}s";

        var outgoing = _game.OutgoingTorpedoes;
        if (outgoing > 0)
        {
            TubesText.Text += $"   {outgoing} away";
        }

        TubesText.Foreground = player.TubesLoaded > 0 ? Healthy : Warning;

        var ordnance = player.Ordnance;
        OrdnanceText.Text =
            $"Load    {GameSession.OrdnanceLabel(player.TorpedoClass)}"
            + $"   dmg {ordnance.Damage}   spd {ordnance.Speed}";
        OrdnanceText.Foreground =
            player.TorpedoClass is DtrekWeaponClass.Photon
                or DtrekWeaponClass.Missile
                or DtrekWeaponClass.Plasma
                ? DefaultText
                : Warning;

        LockText.Text = _game.Locked
            ? $"Lock    {enemy.Name} {_game.EnemyRange}"
            : ordnance.Tracking
                ? "Lock    none"
                : "Lock    n/a";
        LockText.Foreground = _game.Locked ? Healthy : DefaultText;

        var orbitPlanet = _game.OrbitPlanet;
        OrbitText.Text = _game.Orbiting
            ? $"Orbit   {orbitPlanet?.Name ?? "unknown"}"
            : "Orbit   none";
        OrbitText.Foreground = _game.Orbiting ? Healthy : DefaultText;

        var nearest = _game.NearestPlanet;
        PlanetText.Text = nearest is not null
            ? $"Nearest {nearest.Name}  {_game.NearestPlanetDistance}"
            : "Nearest --";
        PlanetText.Foreground = nearest is not null
            && _game.NearestPlanetDistance <= DtrekConstants.EnterOrbitDistance
                ? Healthy
                : DefaultText;

        var services = new List<string>();
        if (_game.AtFuelPlanet)
        {
            services.Add("fuel");
        }

        if (_game.AtRepairPlanet)
        {
            services.Add("repair");
        }

        if (_game.RepairMode)
        {
            services.Add("damage control");
        }

        ServiceText.Text = services.Count > 0
            ? $"Service {string.Join(", ", services)}"
            : "Service none";
        ServiceText.Foreground = _game.RepairMode ? Healthy : DefaultText;

        var incoming = _game.IncomingTorpedoes;
        ThreatText.Text = incoming > 0
            ? $"Incoming {incoming} torpedo{(incoming == 1 ? "" : "s")}"
            : "Incoming none";
        ThreatText.Foreground = incoming > 0 ? Critical : DefaultText;

        UpdateTarget(enemy);

        var log = _game.Log;
        MessageText.Text = string.Join(
            Environment.NewLine, log.TakeLast(7));

        View.ShowGalaxyMap = _showGalaxyMap;
        string? overlay = null;
        string? subtitle = null;
        if (_showGalaxyMap)
        {
            overlay = "GALAXY MAP";
            subtitle = "G to close";
        }
        else if (_game.Paused)
        {
            overlay = "PAUSED";
            subtitle = "P to resume";
        }
        else if (_game.IsPlayerDestroyed)
        {
            overlay = "SHIP LOST";
            subtitle = "R to restart";
        }
        else if (_game.IsEnemyDestroyed)
        {
            overlay = "TARGET DESTROYED";
            subtitle = "R for a new patrol";
        }

        View.SetScene(
            snapshot,
            _game.PlayerSlot,
            _game.EnemySlot,
            _game.Beams,
            snapshot.Tick,
            overlay,
            subtitle);
    }

    private (string Text, Brush Brush) ResolveStatus()
    {
        if (_game.Paused)
        {
            return ("PAUSED", Warning);
        }

        if (_game.IsPlayerDestroyed)
        {
            return ("SHIP LOST", Critical);
        }

        if (_game.IsEnemyDestroyed)
        {
            return ("TARGET DESTROYED", Healthy);
        }

        if (_game.Orbiting
            && _game.IncomingTorpedoes == 0
            && _game.EnemyRange > 7500)
        {
            return (_game.RepairMode ? "IN ORBIT / REPAIR" : "IN ORBIT", Healthy);
        }

        return _game.EnemyRange <= 7500 || _game.IncomingTorpedoes > 0
            ? ("RED ALERT", Critical)
            : ("PATROL", Healthy);
    }

    private void UpdateTarget(ShipSnapshot enemy)
    {
        if (!enemy.InUse || enemy.Status == DtrekPlayerStatus.Dead)
        {
            TargetNameText.Text = "No contact";
            TargetRangeText.Text = "Range   --";
            TargetStatusText.Text = "Hull    --";
            TargetShieldText.Text = "Screens --";
            TargetAiText.Text = "AI      --";
            TargetNameText.Foreground = DefaultText;
            TargetRangeText.Foreground = DefaultText;
            TargetAiText.Foreground = DefaultText;
            return;
        }

        TargetNameText.Text = $"{enemy.Name}  ({enemy.Team})";
        TargetRangeText.Text = $"Range   {_game.EnemyRange} / 7500";
        TargetRangeText.Foreground =
            _game.EnemyRange <= 7500 ? Warning : DefaultText;
        TargetStatusText.Text =
            $"Hull    {enemy.Hull}/{enemy.HullMaximum}   {enemy.Status}";
        TargetShieldText.Text =
            $"Screens {(enemy.ShieldsUp ? "UP" : "DOWN")}";

        if (enemy.IsRobot)
        {
            var behavior = new List<string> { enemy.RobotState.ToString() };
            if (enemy.RobotTargetSlot == _game.PlayerSlot)
            {
                behavior.Add("locked on you");
            }

            if (enemy.RobotPatrolPlanetSlot >= 0)
            {
                var planet = _game.Snapshot.FindPlanet(
                    enemy.RobotPatrolPlanetSlot);
                behavior.Add($"patrol "
                    + (planet?.Name
                        ?? enemy.RobotPatrolPlanetSlot.ToString()));
            }

            TargetAiText.Text =
                $"AI      {string.Join(" / ", behavior)}"
                + $"   aggr {enemy.RobotOffense}";
            TargetAiText.Foreground =
                enemy.RobotState is DtrekRobotState.Attack
                    or DtrekRobotState.Evade
                    ? Warning
                    : DefaultText;
        }
        else
        {
            TargetAiText.Text = "AI      --";
            TargetAiText.Foreground = DefaultText;
        }
    }

    private void UpdateDamageControl(ShipSnapshot player)
    {
        var damaged = player.Systems.Where(system => system.IsDamaged).ToArray();
        DamageSummaryText.Text =
            $"Systems {player.SystemSize} kt   "
            + $"{damaged.Length} unit{(damaged.Length == 1 ? "" : "s")} damaged";
        DamageSummaryText.Foreground =
            damaged.Length == 0 ? Healthy : Warning;
        DamageEmptyText.Visibility =
            damaged.Length == 0 ? Visibility.Visible : Visibility.Collapsed;

        DamagePanel.Children.Clear();
        for (var i = 0; i < damaged.Length && i < 8; i++)
        {
            var system = damaged[i];
            var row = new TextBlock
            {
                FontSize = 12,
                Margin = new Thickness(0, 1, 0, 1),
                Foreground = Health(system.Size, system.SizeMaximum),
                Text = $"{GameSession.SystemLabel(system),-11}"
                    + $"{Bar(system.Size, system.SizeMaximum, 8)} "
                    + $"{system.Size,3}/{system.SizeMaximum}",
            };
            DamagePanel.Children.Add(row);
        }

        if (damaged.Length > 8)
        {
            DamagePanel.Children.Add(new TextBlock
            {
                FontSize = 11,
                Margin = new Thickness(0, 1, 0, 1),
                Foreground = Warning,
                Text = $"+{damaged.Length - 8} more damaged",
            });
        }
    }

    private static void SetShield(
        TextBlock target, string label, int value, int maximum)
    {
        target.Text = $"{label,-6}{Bar(value, maximum)} {value,3}/{maximum}";
        target.Foreground = Health(value, maximum);
    }

    private static string Row(string label, int value, int maximum)
        => $"{label,-6}{Bar(value, maximum)} {value,4}/{maximum}";

    private static string Bar(int value, int maximum, int width = 10)
    {
        if (maximum <= 0)
        {
            return new string('\u2591', width);
        }

        var ratio = Math.Clamp(value / (double)maximum, 0.0, 1.0);
        var filled = (int)Math.Round(ratio * width);
        return new string('\u2588', filled)
            + new string('\u2591', width - filled);
    }

    private static Brush Health(int value, int maximum)
    {
        if (maximum <= 0)
        {
            return Critical;
        }

        var ratio = value / (double)maximum;
        if (ratio > 0.5)
        {
            return Healthy;
        }

        return ratio > 0.25 ? Warning : Critical;
    }

    private bool _showGalaxyMap;

    private void OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.G && !e.IsRepeat)
        {
            _showGalaxyMap = !_showGalaxyMap;
            RefreshUi();
            e.Handled = true;
            return;
        }

        if (!_game.IsMissionActive && !_game.Paused)
        {
            switch (e.Key)
            {
                case Key.D1:
                case Key.NumPad1:
                    if (!e.IsRepeat)
                    {
                        _game.StartMission(DtrekTeam.Federation);
                        RefreshUi();
                    }
                    e.Handled = true;
                    break;
                case Key.D2:
                case Key.NumPad2:
                    if (!e.IsRepeat)
                    {
                        _game.StartMission(DtrekTeam.Romulan);
                        RefreshUi();
                    }
                    e.Handled = true;
                    break;
                case Key.D3:
                case Key.NumPad3:
                    if (!e.IsRepeat)
                    {
                        _game.StartMission(DtrekTeam.Klingon);
                        RefreshUi();
                    }
                    e.Handled = true;
                    break;
                case Key.D4:
                case Key.NumPad4:
                    if (!e.IsRepeat)
                    {
                        _game.StartMission(DtrekTeam.Orion);
                        RefreshUi();
                    }
                    e.Handled = true;
                    break;
                case Key.D5:
                case Key.NumPad5:
                    if (!e.IsRepeat)
                    {
                        _game.StartMission(DtrekTeam.Independent);
                        RefreshUi();
                    }
                    e.Handled = true;
                    break;
            }
            return;
        }

        switch (e.Key)
        {
            case Key.A:
            case Key.Left:
            case Key.D:
            case Key.Right:
            case Key.W:
            case Key.Up:
            case Key.S:
            case Key.Down:
                _held.Add(e.Key);
                e.Handled = true;
                break;

            case Key.Space:
                if (!e.IsRepeat)
                {
                    _game.FirePlayerPhasers();
                    RefreshUi();
                }

                e.Handled = true;
                break;

            case Key.F:
                if (!e.IsRepeat)
                {
                    _game.FirePlayerTorpedo();
                    RefreshUi();
                }

                e.Handled = true;
                break;

            case Key.Tab:
                if (!e.IsRepeat)
                {
                    _game.ToggleShields();
                    RefreshUi();
                }

                e.Handled = true;
                break;

            case Key.D1:
            case Key.NumPad1:
                SelectOrdnance(DtrekWeaponClass.Photon, e);
                break;

            case Key.D2:
            case Key.NumPad2:
                SelectOrdnance(DtrekWeaponClass.Missile, e);
                break;

            case Key.D3:
            case Key.NumPad3:
                SelectOrdnance(DtrekWeaponClass.Plasma, e);
                break;

            case Key.D4:
            case Key.NumPad4:
                SelectOrdnance(DtrekWeaponClass.Chaff, e);
                break;

            case Key.D5:
            case Key.NumPad5:
                SelectOrdnance(DtrekWeaponClass.Mine, e);
                break;

            case Key.L:
                if (!e.IsRepeat)
                {
                    _game.ToggleLock();
                    RefreshUi();
                }

                e.Handled = true;
                break;

            case Key.O:
                if (!e.IsRepeat)
                {
                    _game.ToggleOrbit();
                    RefreshUi();
                }

                e.Handled = true;
                break;

            case Key.T:
                if (!e.IsRepeat)
                {
                    _game.ToggleRepair();
                    RefreshUi();
                }

                e.Handled = true;
                break;

            case Key.P:
                if (!e.IsRepeat)
                {
                    _game.TogglePause();
                    RefreshUi();
                }

                e.Handled = true;
                break;

            case Key.R:
                if (!e.IsRepeat)
                {
                    _held.Clear();
                    _game.Restart();
                    RefreshUi();
                }

                e.Handled = true;
                break;
        }
    }

    private void SelectOrdnance(DtrekWeaponClass weaponClass, KeyEventArgs e)
    {
        if (!e.IsRepeat)
        {
            _game.SelectOrdnance(weaponClass);
            RefreshUi();
        }

        e.Handled = true;
    }

    private void OnPreviewKeyUp(object sender, KeyEventArgs e)
    {
        _held.Remove(e.Key);
    }

    private void OnDeactivated(object? sender, EventArgs e)
    {
        _held.Clear();
    }

    private GameSnapshot CreateEmptySnapshot()
    {
        var engine = new DtrekEngine(1989);
        engine.GenerateGalaxy(1989);
        engine.Step(0);
        var snapshot = engine.CaptureSnapshot();
        engine.Dispose();
        return snapshot;
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        try
        {
            _timer.Stop();
        }
        catch
        {
        }

        try
        {
            _game.Dispose();
        }
        catch
        {
        }
    }

    private static SolidColorBrush MakeBrush(byte red, byte green, byte blue)
    {
        var brush = new SolidColorBrush(Color.FromRgb(red, green, blue));
        brush.Freeze();
        return brush;
    }
}
