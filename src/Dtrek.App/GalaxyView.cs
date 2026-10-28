using System.Globalization;
using System.Windows;
using System.Windows.Media;
using Dtrek.Core;

namespace Dtrek.App;

/// <summary>
/// Retro vector display: star field, galaxy grid, ships, phaser beams,
/// photon torpedoes, and mission overlays. The camera follows the player.
/// </summary>
public sealed class GalaxyView : FrameworkElement
{
    private const double ViewSpan = 40000.0;
    private const int GridStep = 10000;
    private const int PhaserRange = 7500;

    private readonly Brush _background = MakeBrush(255, 4, 7, 12);
    private readonly Brush _star = MakeBrush(170, 150, 185, 220);
    private readonly Brush _starDim = MakeBrush(95, 90, 115, 150);
    private readonly Pen _gridPen = MakePen(255, 18, 31, 47, 1);
    private readonly Pen _borderPen = MakePen(150, 52, 94, 132, 1.5, 6, 4);
    private readonly Pen _rangePen = MakePen(55, 70, 200, 190, 1, 3, 6);
    private readonly Pen _headingPen = MakePen(80, 90, 220, 200, 1, 2, 5);
    private readonly Pen _playerPen = MakePen(255, 90, 255, 198, 1.6);
    private readonly Brush _playerFill = MakeBrush(80, 60, 230, 160);
    private readonly Pen _enemyPen = MakePen(255, 255, 92, 92, 1.6);
    private readonly Brush _enemyFill = MakeBrush(80, 230, 70, 70);
    private readonly Pen _shieldPen = MakePen(255, 102, 200, 255, 1.4);
    private readonly Brush _shieldFill = MakeBrush(40, 120, 200, 255);
    private readonly Pen _playerBeamPen = MakePen(235, 120, 255, 220, 2.4);
    private readonly Pen _enemyBeamPen = MakePen(235, 255, 92, 92, 2.4);
    private readonly Pen _missBeamPen = MakePen(120, 150, 175, 195, 1.2);

    private readonly Pen _playerTorpedoPen = MakePen(255, 255, 232, 150, 1.4);
    private readonly Brush _playerTorpedoFill = MakeBrush(210, 255, 240, 170);
    private readonly Pen _playerTorpedoTrackPen = MakePen(90, 255, 214, 110, 1.2);
    private readonly Pen _enemyTorpedoPen = MakePen(255, 255, 130, 90, 1.4);
    private readonly Brush _enemyTorpedoFill = MakeBrush(210, 255, 140, 100);
    private readonly Pen _enemyTorpedoTrackPen = MakePen(90, 240, 110, 80, 1.2);
    private readonly Pen _torpedoBlastPen = MakePen(235, 255, 180, 70, 2.2);
    private readonly Brush _torpedoBlastFill = MakeBrush(180, 255, 220, 120);
    private readonly Pen _plasmaPen = MakePen(230, 195, 135, 255, 1.8);
    private readonly Brush _plasmaFill = MakeBrush(200, 215, 165, 255);
    private readonly Pen _missilePen = MakePen(240, 255, 196, 104, 1.2);
    private readonly Brush _missileFill = MakeBrush(220, 255, 214, 132);
    private readonly Pen _chaffPen = MakePen(200, 195, 212, 194, 1.0);
    private readonly Brush _chaffFill = MakeBrush(140, 224, 232, 214);
    private readonly Pen _minePen = MakePen(240, 255, 132, 92, 1.3);
    private readonly Brush _mineFill = MakeBrush(180, 242, 104, 72);
    private readonly Pen _guidancePen = MakePen(90, 118, 220, 198, 1.0, 3, 5);

    private readonly Pen _explosionPen = MakePen(210, 255, 158, 52, 2);
    private readonly Brush _labelBrush = MakeBrush(235, 202, 223, 238);
    private readonly Brush _dimLabelBrush = MakeBrush(155, 140, 160, 182);
    private readonly Brush _overlayPanel = MakeBrush(200, 10, 17, 27);
    private readonly Brush _overlayBrush = MakeBrush(255, 120, 255, 200);
    private readonly Brush _overlaySubtitleBrush = MakeBrush(255, 198, 214, 230);

    private readonly Pen _planetPen = MakePen(200, 118, 176, 210, 1.2);
    private readonly Brush _planetFill = MakeBrush(90, 92, 138, 168);
    private readonly Pen _homePen = MakePen(255, 196, 240, 255, 2);
    private readonly Pen _fuelPen = MakePen(255, 102, 255, 190, 1.6);
    private readonly Pen _repairPen = MakePen(255, 255, 202, 104, 1.6);
    private readonly Pen _blackHolePen = MakePen(255, 198, 154, 255, 1.8);
    private readonly Brush _blackHoleFill = MakeBrush(240, 7, 4, 13);
    private readonly Pen _pulsarPen = MakePen(255, 154, 232, 255, 1.6);
    private readonly Brush _pulsarFill = MakeBrush(255, 232, 242, 255);
    private readonly Brush _nebulaFill = MakeBrush(38, 118, 88, 178);
    private readonly Pen _nebulaPen = MakePen(74, 130, 100, 190, 1);
    private readonly Pen _orbitPen = MakePen(180, 112, 255, 190, 1.2, 3, 4);

    private readonly Brush _federationFill = MakeBrush(120, 96, 200, 255);
    private readonly Pen _federationPen = MakePen(255, 110, 210, 255, 1.4);
    private readonly Brush _romulanFill = MakeBrush(120, 255, 120, 120);
    private readonly Pen _romulanPen = MakePen(255, 255, 130, 130, 1.4);
    private readonly Brush _klingonFill = MakeBrush(120, 255, 178, 98);
    private readonly Pen _klingonPen = MakePen(255, 255, 190, 120, 1.4);
    private readonly Brush _orionFill = MakeBrush(120, 188, 150, 255);
    private readonly Pen _orionPen = MakePen(255, 202, 168, 255, 1.4);
    private readonly Brush _independentFill = MakeBrush(120, 198, 206, 176);
    private readonly Pen _independentPen = MakePen(255, 212, 220, 190, 1.4);

    private readonly Point[] _stars = CreateStars(2000);

    private double _scale = 1;
    private double _cameraX;
    private double _cameraY;
    private double _width;
    private double _height;

    public GalaxyView()
    {
        ClipToBounds = true;
        SnapsToDevicePixels = true;
    }

    public GameSnapshot? Snapshot { get; private set; }

    public int PlayerSlot { get; private set; }

    public int EnemySlot { get; private set; }

    public IReadOnlyList<BeamEffect> Beams { get; private set; }
        = Array.Empty<BeamEffect>();

    public int Tick { get; private set; }

    public string? Overlay { get; private set; }

    public string? OverlaySubtitle { get; private set; }

    public void SetScene(
        GameSnapshot snapshot,
        int playerSlot,
        int enemySlot,
        IReadOnlyList<BeamEffect> beams,
        int tick,
        string? overlay = null,
        string? subtitle = null)
    {
        Snapshot = snapshot;
        PlayerSlot = playerSlot;
        EnemySlot = enemySlot;
        Beams = beams;
        Tick = tick;
        Overlay = overlay;
        OverlaySubtitle = subtitle;
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext dc)
    {
        var width = ActualWidth;
        var height = ActualHeight;
        if (width < 2 || height < 2)
        {
            return;
        }

        dc.DrawRectangle(_background, null, new Rect(0, 0, width, height));

        if (Snapshot is not { } snapshot)
        {
            return;
        }

        _width = width;
        _height = height;
        _scale = Math.Min(width, height) / ViewSpan;
        _cameraX = DtrekConstants.GalaxyWidth / 2.0;
        _cameraY = DtrekConstants.GalaxyWidth / 2.0;

        var player = default(ShipSnapshot);
        var hasPlayer = PlayerSlot >= 0 && PlayerSlot < DtrekConstants.MaxPlayers;
        if (hasPlayer)
        {
            try
            {
                player = snapshot.GetShip(PlayerSlot);
                if (player.InUse)
                {
                    _cameraX = player.X;
                    _cameraY = player.Y;
                }
            }
            catch
            {
                hasPlayer = false;
                player = default;
            }
        }

        DrawStars(dc);
        DrawGrid(dc);
        DrawGalaxyBorder(dc);
        DrawPlanets(dc);
        if (hasPlayer && player.InUse)
        {
            DrawOrbitRing(dc, player);

            var center = ToScreen(player.X, player.Y);
            dc.DrawEllipse(
                null, _rangePen, center,
                PhaserRange * _scale, PhaserRange * _scale);

            var (dx, dy) = Navigation.CourseVector(player.Course);
            dc.DrawLine(_headingPen, center, new Point(
                center.X + dx * PhaserRange * _scale,
                center.Y + dy * PhaserRange * _scale));
        }

        DrawBeams(dc);
        DrawTorpedoes(dc);

        if (EnemySlot >= 0 && EnemySlot < DtrekConstants.MaxPlayers)
        {
            try
            {
                DrawShip(dc, snapshot.GetShip(EnemySlot), isPlayer: false);
            }
            catch
            {
            }
        }

        if (hasPlayer && player.InUse)
        {
            DrawShip(dc, player, isPlayer: true);
        }

        DrawOverlay(dc, width, height);
    }

    private void DrawStars(DrawingContext dc)
    {
        for (var i = 0; i < _stars.Length; i++)
        {
            var point = ToScreen((int)_stars[i].X, (int)_stars[i].Y);
            if (point.X < 0 || point.X >= _width
                || point.Y < 0 || point.Y >= _height)
            {
                continue;
            }

            var brush = i % 7 == 0 ? _star : _starDim;
            var size = i % 11 == 0 ? 1.8 : 1.2;
            dc.DrawRectangle(brush, null, new Rect(point.X, point.Y, size, size));
        }
    }

    private void DrawGrid(DrawingContext dc)
    {
        var left = _cameraX - _width / 2 / _scale;
        var right = _cameraX + _width / 2 / _scale;
        var top = _cameraY - _height / 2 / _scale;
        var bottom = _cameraY + _height / 2 / _scale;

        for (var x = Math.Ceiling(left / GridStep) * GridStep;
            x <= right; x += GridStep)
        {
            var screenX = (x - _cameraX) * _scale + _width / 2;
            dc.DrawLine(_gridPen,
                new Point(screenX, 0), new Point(screenX, _height));
        }

        for (var y = Math.Ceiling(top / GridStep) * GridStep;
            y <= bottom; y += GridStep)
        {
            var screenY = (y - _cameraY) * _scale + _height / 2;
            dc.DrawLine(_gridPen,
                new Point(0, screenY), new Point(_width, screenY));
        }
    }

    private void DrawGalaxyBorder(DrawingContext dc)
    {
        var topLeft = ToScreen(0, 0);
        var bottomRight = ToScreen(
            DtrekConstants.GalaxyWidth, DtrekConstants.GalaxyWidth);
        dc.DrawRectangle(null, _borderPen, new Rect(topLeft, bottomRight));
    }

    private void DrawPlanets(DrawingContext dc)
    {
        if (Snapshot is not { } snapshot)
        {
            return;
        }

        foreach (var planet in snapshot.Planets)
        {
            var center = ToScreen(planet.X, planet.Y);

            if (planet.IsNebula)
            {
                var radius = DtrekConstants.NebulaRadius * _scale;
                if (center.X + radius < 0 || center.X - radius > _width
                    || center.Y + radius < 0 || center.Y - radius > _height)
                {
                    continue;
                }

                dc.DrawEllipse(_nebulaFill, _nebulaPen, center, radius, radius);
            }

            const double padding = 60;
            if (center.X < -padding || center.X > _width + padding
                || center.Y < -padding || center.Y > _height + padding)
            {
                continue;
            }

            if (planet.IsBlackHole)
            {
                dc.DrawEllipse(_blackHoleFill, _blackHolePen, center, 7, 7);
                dc.DrawEllipse(null, _blackHolePen, center, 11, 11);
                dc.DrawEllipse(null, _planetPen, center, 15, 15);
            }
            else if (planet.IsPulsar)
            {
                dc.DrawEllipse(_pulsarFill, _pulsarPen, center, 3.2, 3.2);
                dc.DrawLine(_pulsarPen,
                    new Point(center.X - 10, center.Y),
                    new Point(center.X + 10, center.Y));
                dc.DrawLine(_pulsarPen,
                    new Point(center.X, center.Y - 10),
                    new Point(center.X, center.Y + 10));
            }
            else
            {
                var (fill, pen) = OwnerStyle(planet.Owner);
                var radius = planet.IsHomeWorld ? 8.5 : 5.5;

                dc.DrawEllipse(
                    fill,
                    planet.IsHomeWorld ? _homePen : pen,
                    center, radius, radius);

                if (planet.IsFuelDepot)
                {
                    dc.DrawEllipse(
                        null, _fuelPen, center, radius + 3.5, radius + 3.5);
                }

                if (planet.IsRepairBase)
                {
                    dc.DrawEllipse(
                        null, _repairPen, center, radius + 6, radius + 6);
                }
            }

            if (!ShouldLabel(planet))
            {
                continue;
            }

            var dpi = VisualTreeHelper.GetDpi(this).PixelsPerDip;
            var label = new FormattedText(
                planet.Name,
                CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight,
                new Typeface("Consolas"),
                10,
                planet.IsBlackHole || planet.IsNebula || planet.IsPulsar
                    ? _dimLabelBrush
                    : _labelBrush,
                dpi);
            dc.DrawText(label, new Point(
                center.X - label.Width / 2, center.Y + 20));
        }
    }

    private void DrawOrbitRing(DrawingContext dc, ShipSnapshot player)
    {
        if (!player.InUse || !player.IsOrbiting || Snapshot is not { } snapshot)
        {
            return;
        }

        var planet = snapshot.FindPlanet(player.OrbitPlanetSlot);
        if (planet is null)
        {
            return;
        }

        var radius = DtrekConstants.OrbitDistance * _scale;
        dc.DrawEllipse(
            null, _orbitPen, ToScreen(planet.X, planet.Y), radius, radius);
    }

    private static bool ShouldLabel(PlanetSnapshot planet)
        => planet.IsHomeWorld
            || planet.IsBlackHole
            || planet.IsPulsar
            || planet.IsNebula
            || planet.IsFuelDepot
            || planet.IsRepairBase;

    private (Brush Fill, Pen Pen) OwnerStyle(DtrekTeam owner) => owner switch
    {
        DtrekTeam.Federation => (_federationFill, _federationPen),
        DtrekTeam.Romulan => (_romulanFill, _romulanPen),
        DtrekTeam.Klingon => (_klingonFill, _klingonPen),
        DtrekTeam.Orion => (_orionFill, _orionPen),
        _ => (_independentFill, _independentPen),
    };

    private void DrawBeams(DrawingContext dc)
    {
        foreach (var beam in Beams)
        {
            if (beam.ExpiresAtTick < Tick)
            {
                continue;
            }

            var pen = !beam.Hit
                ? _missBeamPen
                : beam.FromPlayer ? _playerBeamPen : _enemyBeamPen;

            var start = ToScreen(beam.X1, beam.Y1);
            var end = ToScreen(beam.X2, beam.Y2);
            dc.DrawLine(pen, start, end);

            if (beam.Hit)
            {
                dc.DrawEllipse(pen.Brush, null, end, 3.5, 3.5);
            }
        }
    }

    private void DrawTorpedoes(DrawingContext dc)
    {
        if (Snapshot is not { } snapshot)
        {
            return;
        }

        foreach (var torpedo in snapshot.Torpedoes)
        {
            var center = ToScreen(torpedo.X, torpedo.Y);
            var offscreen = center.X < -60 || center.X > _width + 60
                || center.Y < -60 || center.Y > _height + 60;
            if (offscreen)
            {
                continue;
            }

            if (torpedo.IsDetonating)
            {
                var phase = (10 - Math.Clamp(torpedo.Fuse, 0, 10)) / 10.0;
                var scale = torpedo.WeaponClass == DtrekWeaponClass.Plasma
                    ? 1.4
                    : torpedo.WeaponClass == DtrekWeaponClass.Mine
                        ? 1.2
                        : 1.0;
                var radius = (6 + phase * 22) * scale;
                dc.DrawEllipse(
                    null, _torpedoBlastPen, center, radius, radius);
                dc.DrawEllipse(
                    _torpedoBlastFill, null, center, radius * 0.35, radius * 0.35);
                continue;
            }

            if (!torpedo.IsInFlight)
            {
                continue;
            }

            var fromPlayer = torpedo.OwnerSlot == PlayerSlot;
            var (dx, dy) = Navigation.CourseVector(torpedo.Course);
            var tail = new Point(
                center.X - dx * 16, center.Y - dy * 16);

            if (torpedo.Status == TorpedoStatus.Track
                && torpedo.TargetSlot >= 0
                && torpedo.TargetSlot < snapshot.Ships.Count)
            {
                var target = snapshot.GetShip(torpedo.TargetSlot);
                if (target.InUse && target.Status != DtrekPlayerStatus.Dead)
                {
                    dc.DrawLine(
                        _guidancePen, center, ToScreen(target.X, target.Y));
                }
            }

            switch (torpedo.WeaponClass)
            {
                case DtrekWeaponClass.Plasma:
                    dc.DrawLine(_plasmaPen, tail, center);
                    dc.DrawEllipse(_plasmaFill, _plasmaPen, center, 5, 5);
                    dc.DrawEllipse(
                        null, _plasmaPen, center, 8.5, 8.5);
                    break;

                case DtrekWeaponClass.Missile:
                    dc.DrawLine(_missilePen, tail, center);
                    DrawDart(dc, center, dx, dy, _missileFill, _missilePen);
                    break;

                case DtrekWeaponClass.Chaff:
                    DrawChaff(dc, center);
                    break;

                case DtrekWeaponClass.Mine:
                    dc.DrawEllipse(_mineFill, _minePen, center, 3.2, 3.2);
                    dc.DrawLine(_minePen,
                        new Point(center.X - 6, center.Y - 6),
                        new Point(center.X + 6, center.Y + 6));
                    dc.DrawLine(_minePen,
                        new Point(center.X - 6, center.Y + 6),
                        new Point(center.X + 6, center.Y - 6));
                    if (torpedo.Arm > 0)
                    {
                        dc.DrawEllipse(
                            null, _guidancePen, center, 9, 9);
                    }

                    break;

                default:
                    dc.DrawLine(
                        fromPlayer
                            ? _playerTorpedoTrackPen
                            : _enemyTorpedoTrackPen,
                        tail, center);
                    dc.DrawEllipse(
                        fromPlayer ? _playerTorpedoFill : _enemyTorpedoFill,
                        fromPlayer ? _playerTorpedoPen : _enemyTorpedoPen,
                        center, 3, 3);
                    break;
            }
        }
    }

    private static void DrawDart(
        DrawingContext dc,
        Point center,
        double dx,
        double dy,
        Brush fill,
        Pen pen)
    {
        const double length = 6.5;
        var nose = new Point(
            center.X + dx * length, center.Y + dy * length);
        var back = new Point(
            center.X - dx * length * 0.7, center.Y - dy * length * 0.7);
        var perpX = -dy * 3.4;
        var perpY = dx * 3.4;

        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            context.BeginFigure(nose, isFilled: true, isClosed: true);
            context.LineTo(
                new Point(back.X + perpX, back.Y + perpY),
                isStroked: true, isSmoothJoin: false);
            context.LineTo(
                new Point(back.X - perpX, back.Y - perpY),
                isStroked: true, isSmoothJoin: false);
        }

        geometry.Freeze();
        dc.DrawGeometry(fill, pen, geometry);
    }

    private void DrawChaff(DrawingContext dc, Point center)
    {
        var offsets = new (double X, double Y)[]
        {
            (-5, -3), (4, -4), (-2, 4), (6, 3), (-7, 2), (1, -7),
        };

        foreach (var (x, y) in offsets)
        {
            dc.DrawEllipse(
                _chaffFill, _chaffPen,
                new Point(center.X + x, center.Y + y), 1.6, 1.6);
        }
    }

    private void DrawShip(DrawingContext dc, ShipSnapshot ship, bool isPlayer)
    {
        if (!ship.InUse || ship.Status == DtrekPlayerStatus.Dead)
        {
            return;
        }

        var center = ToScreen(ship.X, ship.Y);
        if (center.X < -80 || center.X > _width + 80
            || center.Y < -80 || center.Y > _height + 80)
        {
            return;
        }

        if (ship.Status == DtrekPlayerStatus.Exploding)
        {
            var phase = (Tick % 10) / 10.0;
            var radius = 5 + phase * 22;
            dc.DrawEllipse(null, _explosionPen, center, radius, radius);
            return;
        }

        var (dx, dy) = Navigation.CourseVector(ship.Course);
        var length = isPlayer ? 15.0 : 14.0;
        var nose = new Point(
            center.X + dx * length, center.Y + dy * length);
        var tail = new Point(
            center.X - dx * length * 0.6, center.Y - dy * length * 0.6);
        var perpX = -dy * length * 0.55;
        var perpY = dx * length * 0.55;
        var left = new Point(tail.X + perpX, tail.Y + perpY);
        var right = new Point(tail.X - perpX, tail.Y - perpY);

        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            context.BeginFigure(nose, isFilled: true, isClosed: true);
            context.LineTo(left, isStroked: true, isSmoothJoin: false);
            context.LineTo(right, isStroked: true, isSmoothJoin: false);
        }

        geometry.Freeze();
        dc.DrawGeometry(
            isPlayer ? _playerFill : _enemyFill,
            isPlayer ? _playerPen : _enemyPen,
            geometry);

        if (ship.ShieldsUp)
        {
            var shieldRadius = length * 1.6;
            dc.DrawEllipse(_shieldFill, _shieldPen, center, shieldRadius, shieldRadius);
        }

        var dpi = VisualTreeHelper.GetDpi(this).PixelsPerDip;
        var label = new FormattedText(
            $"{ship.Name}  {ship.Speed}w",
            CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight,
            new Typeface("Consolas"),
            11,
            isPlayer ? _labelBrush : _dimLabelBrush,
            dpi);
        dc.DrawText(label, new Point(
            center.X - label.Width / 2, center.Y + length + 8));
    }

    private void DrawOverlay(DrawingContext dc, double width, double height)
    {
        if (string.IsNullOrEmpty(Overlay))
        {
            return;
        }

        var dpi = VisualTreeHelper.GetDpi(this).PixelsPerDip;
        var typeface = new Typeface(
            new FontFamily("Consolas"),
            FontStyles.Normal,
            FontWeights.Bold,
            FontStretches.Normal);

        var title = new FormattedText(
            Overlay,
            CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight,
            typeface,
            40,
            _overlayBrush,
            dpi);

        FormattedText? subtitle = null;
        if (!string.IsNullOrEmpty(OverlaySubtitle))
        {
            subtitle = new FormattedText(
                OverlaySubtitle,
                CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight,
                new Typeface("Consolas"),
                14,
                _overlaySubtitleBrush,
                dpi);
        }

        var blockWidth = Math.Max(title.Width, subtitle?.Width ?? 0);
        var blockHeight = title.Height + (subtitle is null ? 0 : subtitle.Height + 6);
        var panel = new Rect(
            (width - blockWidth) / 2 - 28,
            (height - blockHeight) / 2 - 20,
            blockWidth + 56,
            blockHeight + 40);

        dc.DrawRoundedRectangle(_overlayPanel, null, panel, 8, 8);
        dc.DrawText(title, new Point(
            (width - title.Width) / 2, panel.Top + 20));

        if (subtitle is not null)
        {
            dc.DrawText(subtitle, new Point(
                (width - subtitle.Width) / 2,
                panel.Top + 20 + title.Height + 6));
        }
    }

    private Point ToScreen(int x, int y)
        => new(
            (x - _cameraX) * _scale + _width / 2,
            (y - _cameraY) * _scale + _height / 2);

    private static Point[] CreateStars(int count)
    {
        var random = new Random(1989);
        var stars = new Point[count];
        for (var i = 0; i < count; i++)
        {
            stars[i] = new Point(
                random.Next(DtrekConstants.GalaxyWidth),
                random.Next(DtrekConstants.GalaxyWidth));
        }

        return stars;
    }

    private static SolidColorBrush MakeBrush(byte alpha, byte red, byte green, byte blue)
    {
        var brush = new SolidColorBrush(Color.FromArgb(alpha, red, green, blue));
        brush.Freeze();
        return brush;
    }

    private static Pen MakePen(
        byte alpha, byte red, byte green, byte blue, double thickness,
        double dashLength = 0, double gapLength = 0)
    {
        var pen = new Pen(MakeBrush(alpha, red, green, blue), thickness);
        if (dashLength > 0 && gapLength > 0)
        {
            pen.DashStyle = new DashStyle(new double[] { dashLength, gapLength }, 0);
        }

        pen.Freeze();
        return pen;
    }
}
