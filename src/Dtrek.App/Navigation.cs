namespace Dtrek.App;

/// <summary>
/// Course helpers for the original 256-step compass: 0 is up, 64 is right.
/// </summary>
internal static class Navigation
{
    public static (double X, double Y) CourseVector(int course)
    {
        var angle = course * Math.PI * 2.0 / 256.0;
        return (Math.Sin(angle), -Math.Cos(angle));
    }

    public static int CourseTo(int x1, int y1, int x2, int y2)
    {
        var dx = x2 - x1;
        var dy = y2 - y1;
        var angle = Math.Atan2(dx, -dy);
        if (angle < 0)
        {
            angle += Math.PI * 2.0;
        }

        return (int)Math.Round(angle * 256.0 / (Math.PI * 2.0)) & 255;
    }

    public static int CourseDelta(int a, int b)
    {
        var delta = Math.Abs((a - b) & 255);
        return Math.Min(delta, 256 - delta);
    }

    public static double Distance(int x1, int y1, int x2, int y2)
    {
        var dx = (double)x2 - x1;
        var dy = (double)y2 - y1;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    public static double Distance(
        Dtrek.Core.ShipSnapshot a, Dtrek.Core.ShipSnapshot b)
        => Distance(a.X, a.Y, b.X, b.Y);
}
