# Student CoM Kinematic Plugin

Character animation plugin for the N8RO simulation platform. Implements kinematic joint-angle control with Center of Mass (CoM) estimation for balance-aware human locomotion.

## Overview

This plugin registers three animation evaluators on the `animationModelNathanHuman` model type:

| Animation Code       | Description                              |
|----------------------|------------------------------------------|
| `Student CoM Walk`   | Bipedal walking with CoM-based balance   |
| `Student CoM Push`   | Two-handed pushing motion                |
| `Student CoM Climb`  | Stepping/climbing motion                 |

All motions are purely kinematic — joint angles are computed directly from gait phase and anthropometric parameters. No forces, torques, or rigid-body dynamics are involved.

## Architecture

The plugin follows the N8RO `IPlugin` / `IAnimationModel` interface:

1. `create_plugin()` — factory export
2. `initialize()` — registers the three animation evaluators on the Nathan human model prototype
3. Each evaluator receives `AnimationModelInput` (simulation time, joint list) and returns `AnimationModelOutput` (sparse joint-angle overrides in radians)

### Walk Pipeline

```
simulationTime → gaitPhase → nominalPose (sinusoidal joint curves)
                                   ↓
                          inferContactState (smooth foot weights)
                                   ↓
                          estimateCenterOfMass (sagittal/frontal model)
                                   ↓
                          applyBalanceCorrection (trunk roll/pitch adjust)
                                   ↓
                          joint overrides output
```

### Driven Joints

Hips, knees, ankles, shoulders, elbows (10 major joints) plus spine and head for trunk balance — all as Euler angle overrides.

## Build

Requires:
- MSVC v143+ (Visual Studio 2022 / Build Tools)
- CMake 3.20+
- N8RO release at `C:\N8RO` (or pass `-N8roRelease <path>`)

```powershell
.\scripts\build.ps1
```

Output DLL: `build-nmake\bin\student-com-kinematic-plugin.dll`

## Deploy

Copy the built DLL to the N8RO user plugins directory:

```
C:\N8RO\userPlugins\sim\student-com-kinematic-plugin.dll
```

Restart N8RO. The plugin loads automatically. Select one of the `Student CoM *` animations on a Nathan human entity through the animation component.

## Verify

1. Launch N8RO, click **Active** (top-left)
2. Open **Scenario Editor** and **Simulation Control**
3. Load the `GenericCivillianPresence` scenario
4. Hit **Run** in Simulation Control
5. Press **G** (top-right) → select **GLB** option
6. Select the human entity → assign a `Student CoM Walk` / `Push` / `Climb` animation
7. Observe motion in the GLB viewer

## Design Rationale

See [`docs/design.md`](docs/design.md) for CoM estimation approach, gait parametrization, and balance correction strategy.
