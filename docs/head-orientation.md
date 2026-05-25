# Head Orientation — How It Works

## The sensor

The BNO085 chip measures orientation relative to gravity. It knows which way is "down" at all times. It reports three values:

- **Pitch (sensor)** — tilt on one axis
- **Roll (sensor)** — tilt on the other axis
- **Yaw (sensor)** — rotation around the vertical axis (like a compass heading)

## The problem: we don't know how the chip sits in the casing

The chip is glued inside a plastic casing in some unknown rotation. This means we don't know which sensor axis maps to which head movement. What the sensor calls "pitch" might actually be the head tilting sideways, or forward, depending on how the chip is mounted.

## What the horse's head can do

- **Pitch (horse)** — nose pointing up or down (like nodding yes)
- **Roll (horse)** — head tilting sideways, ear toward shoulder (like tilting an airplane wing)
- **Yaw (horse)** — head turning left or right (like saying no) — this is compass direction

## What we actually care about for lying down detection

When a horse lies down on its side, the head **rolls (horse)** heavily to one side — the ear goes toward the ground. This is a large, sustained change in orientation.

The problem: we don't know if roll (horse) shows up as pitch (sensor), roll (sensor), or a mix of both, because we don't know the chip orientation.

## How to figure it out

Look at the sensor data during a confirmed lying-down period. One or both of pitch (sensor) / roll (sensor) will show a large, sustained value that is clearly different from when the horse is standing. That's your roll (horse) signal.

## Tilt — a shortcut that avoids the chip orientation problem

Tilt is calculated as:

```
tilt = sqrt(pitch² + roll²)
```

This is Pythagoras applied to angles. It measures the total deviation from level, regardless of which direction. It doesn't matter how the chip is rotated in the casing — tilt will always be large when the head is far from level.

- Horse standing normally: tilt ≈ 17–28° (head slightly forward and down is normal)
- Horse lying on its side: tilt expected to be much larger (60–90°+)

Tilt doesn't tell you which direction the head tilted, just how far. But for detecting lying down, that's enough.

## What the overnight data will tell us

By leaving the collar on overnight and looking at the data:

1. **If the horse lies down** — we'll see a long period of low acceleration (still) with high tilt. We can measure the exact tilt range for lying down and use it as a threshold.

2. **If the horse never lies down** — we still have a solid baseline for "normal upright active head." Any sustained reading outside that range is unusual and worth flagging.

## Bonus: compass direction

Yaw (sensor) tells you which direction the horse's nose is pointing — north, east, west, etc. Not useful for health monitoring, but interesting.
