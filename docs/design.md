# Design Document — CoM Kinematic Controller

## Scope

This plugin implements a **kinematic-only** animation controller per the revised assignment scope:

- No forces, torques, PD controllers, or rigid-body dynamics
- Joint angles are computed directly from gait phase using sinusoidal functions
- A simplified Center of Mass (CoM) model informs balance corrections
- Contact state is inferred from gait phase via smooth transitions

## Gait Parametrization

Walking uses a single phase variable derived from simulation time:

```
phase = fmod(t * stepFrequency, 1.0)    // stepFrequency = 1.25 Hz
cycle = phase * 2π
```

Joint angles are then computed as sinusoidal functions of `cycle`:

| Joint        | Formula                              | Range (rad)    |
|--------------|--------------------------------------|----------------|
| Hip pitch    | ±0.34 × sin(cycle)                   | [-0.34, 0.34]  |
| Knee         | 0.18 + 0.58 × max(0, ±sin(cycle))   | [0.18, 0.76]   |
| Ankle        | -0.12 - 0.25 × max(0, ±sin(cycle))  | [-0.37, -0.12] |
| Arm swing    | -0.36 × sin(cycle) (on shoulder Z)   | [-0.36, 0.36]  |
| Elbow        | 0.48 + 0.10 × ½(1 - cos(2·cycle))   | [0.48, 0.58]   |

The elbow uses `½(1 - cos(2θ))` instead of `|sin(θ)|` to avoid derivative discontinuity at zero crossings, which would cause visible trembling.

## Center of Mass Estimation

A 2D sagittal+frontal CoM model sums weighted segment contributions:

| Segment      | Mass fraction | Position source            |
|--------------|:------------:|----------------------------|
| Trunk+head   | 0.54         | pelvisX + trunk lean       |
| Pelvis       | 0.14         | pelvisX                    |
| Left thigh   | 0.10         | leftHipX + hip pitch proj  |
| Right thigh  | 0.10         | rightHipX + hip pitch proj |
| Left shin    | 0.06         | leftHipX + knee angle proj |
| Right shin   | 0.06         | rightHipX + knee angle proj|

Segment lengths: thigh = 0.45 m, shin = 0.45 m (anthropometric estimates).

## Contact State

Foot contact weight transitions use `smoothStep` (Hermite interpolation) instead of hard boolean thresholds:

```
leftFootWeight  = 1 − smoothStep(0.55, 0.70, phase)   // lifts off
                  + smoothStep(0.85, 1.00, phase)      // touches down
```

This eliminates the discrete jumps that caused oscillation in balance corrections.

## Balance Correction

Given the CoM position and the support polygon center, lateral and fore-aft errors drive small corrections:

| Correction     | Gain | Clamp (rad) |
|----------------|:----:|:-----------:|
| Trunk roll     | 0.75 | ±0.12       |
| Trunk pitch    | 0.55 | ±0.10       |
| Hip pitch bias | 0.20 × trunk pitch correction | — |
| Pelvis lateral | 0.015 × (leftWeight − rightWeight) | — |

Gains are intentionally conservative to avoid oscillation.

## Push and Climb Motions

Both use the same joint-override mechanism but with different parametric curves:

- **Push**: forward trunk lean (-0.34 rad), extended shoulder reach, rhythmic knee bend
- **Climb**: alternating high knee lifts (0.42 + 0.66 rad), compensatory arm reach, trunk lean

These motions do not include CoM balance correction — they are shorter-duration actions where static balance is less critical than the walk cycle.
