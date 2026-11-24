# KeyframeIO

Shared scalar automation foundation for offline multimedia. No additional native library required.

```chuck
@import "KeyframeIO";
<<< KeyframeIO.interpolate(0.0, 100.0, 0.25, "smooth") >>>;
<<< KeyframeIO.valueAt("curve.tsv", 2.5) >>>;
```

Each whitespace-separated row in a curve file is time in seconds, scalar value, outgoing interpolation mode. Example:

```text
0 10 linear
2 30 smooth
4 50 step
6 70 linear
```

Modes: step (hold until next key), linear, smooth (smoothstep), easeIn (quadratic), easeOut (quadratic). Each key specifies its outgoing segment; the final mode is parsed but unused. Outside the curve, the nearest endpoint is held. interpolate clamps fraction to [0,1]. Times must be finite, nonnegative, strictly increasing and at most 1e12 seconds. Values must be finite. Maximum 100000 keys and 8 MiB input; LF/CRLF and comments starting with # supported. valid returns 1/0, count returns -1 on error, valueAt/interpolate return NaN on error. File queries reparse each call: not intended for real-time audio. Native consumers can load once and sample in memory using keyframe.h (binary search). This PR does not yet animate any other module, write curves or support multidimensional/quaternion interpolation. Twenty-four numerical and validation smoke checks await execution.
