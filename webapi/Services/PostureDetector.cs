public static class PostureDetector
{
    public static string DetectPostureV3(
        float nqw, float nqx, float nqy, float nqz,
        string currentPosture,
        float rollEnterDeg, float rollExitDeg,
        bool useTiltForPosture = false)
    {
        float gx = 2.0f * (nqx * nqz - nqy * nqw);
        float gy = 2.0f * (nqy * nqz + nqx * nqw);
        float gz = nqw * nqw - nqx * nqx - nqy * nqy + nqz * nqz;

        float rollDeg  = MathF.Asin(Math.Clamp(MathF.Abs(gx), 0f, 1f)) * 180f / MathF.PI;
        float tiltDeg  = MathF.Acos(Math.Clamp(gz, -1f, 1f))           * 180f / MathF.PI;

        float metric = useTiltForPosture ? tiltDeg : rollDeg;

        return currentPosture switch
        {
            "standing" when metric > rollEnterDeg => "lying",
            "lying"    when metric < rollExitDeg  => "standing",
            _                                     => currentPosture
        };
    }
}
