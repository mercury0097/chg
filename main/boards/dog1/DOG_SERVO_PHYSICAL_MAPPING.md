# Dog Servo Physical Mapping

This file records the real-world leg mapping after removing the historical pin remap.

## Real wiring

From `/Users/machenyang/Desktop/chg-API/main/boards/dog1/config.h`:

- `LEFT_FRONT` = `IO18`
- `LEFT_REAR` = `IO17`
- `RIGHT_FRONT` = `IO39`
- `RIGHT_REAR` = `IO38`

## Runtime index order

From `/Users/machenyang/Desktop/chg-API/main/boards/dog1/dog_movements.h`:

- `[0]` = `LEFT_FRONT`
- `[1]` = `LEFT_REAR`
- `[2]` = `RIGHT_FRONT`
- `[3]` = `RIGHT_REAR`

## Real-world direction rule

Neutral is approximately `90`.

- Left front: angle down = forward/outward, angle up = backward/inward
- Left rear: angle down = forward/tuck-under-body, angle up = backward/stretch-back
- Right front: angle up = forward/outward, angle down = backward/inward
- Right rear: angle up = forward/tuck-under-body, angle down = backward/stretch-back

Short version:

- All left legs: smaller angle = forward
- All right legs: larger angle = forward

Canonical formulas:

```txt
left_forward  = neutral - amount
left_backward = neutral + amount
right_forward = neutral + amount
right_backward = neutral - amount
```

## Why this got confusing

The initial baseline used a historical remap in `DogController()`:

```txt
dog_.Init(RIGHT_REAR, RIGHT_FRONT, LEFT_REAR, LEFT_FRONT)
```

That made the old logical array order map to physical legs like this:

- old `[0]` -> physical `RIGHT_FRONT`
- old `[1]` -> physical `RIGHT_REAR`
- old `[2]` -> physical `LEFT_REAR`
- old `[3]` -> physical `LEFT_FRONT`

So the old code comments about "left" and "right" were not describing the real robot.

## How to translate old motion arrays

When converting a target array from the old baseline layout to the new real layout:

```txt
new[LEFT_FRONT]  = old[3]
new[LEFT_REAR]   = old[2]
new[RIGHT_FRONT] = old[0]
new[RIGHT_REAR]  = old[1]
```

Or compactly:

```txt
new = { old[3], old[2], old[0], old[1] }
```

## Sanity checks

These checks match the physical behavior reported on hardware:

- `SleepPose` should be `{50, 50, 130, 130}` so all four legs sprawl forward.
- `PushUp` should move both front legs outward/forward:
  - left front goes below `90`
  - right front goes above `90`
- `WalkForward` must use diagonal pair `LEFT_REAR + RIGHT_FRONT` first.
- `SayHello` keeps the original shipped physical effect: sit on both rear legs, then wave the right front paw.
- `CuriousLean` should move both front legs forward and both rear legs backward.
- `FlinchBack` should move both front legs backward and both rear legs forward.

## Rule for future edits

Do not infer direction from old variable names alone.

Always do these three checks before changing a motion:

1. Confirm the physical leg for each array index.
2. Apply the real direction rule for that side.
3. If preserving an old motion, translate the old target array with `new = { old[3], old[2], old[0], old[1] }`.
