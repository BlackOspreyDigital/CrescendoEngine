# Crescendo Engine

A custom-built, low-level Vulkan graphics engine designed for developers who want absolute authority over their rendering pipeline. 

Crescendo is built on a strict "from-scratch" philosophy. It rejects the modern industry trend of relying on temporal upscaling, DLSS, and "half-measures" to salvage noisy, brute-forced rendering. Instead, the focus is on clever optimization, clean rasterization, and delivering crisp, native-resolution performance reminiscent of the golden era of graphics programming.

## RTS gameplay loop

The RTS controller uses the same paid production and construction rules for both teams. Economy income advances once per simulated second; factories and engineers complete work over time instead of creating AI assets immediately.

Enemy command is split into a deterministic strategic planner and runtime execution:

- phases progress from bootstrap and fortification through expansion, staging, reconnaissance, assault, and recovery;
- utility-scored doctrines choose layered defense, chokepoint denial, mobile reserves, air denial, forward bastions, economic defense, or counterattacks;
- attacks require a stable economy, a staged mixed force, fresh intelligence, and a retained home reserve;
- difficulty changes reaction time and intelligence retention, not costs, income, or build speed;
- `F3` shows the active phase, doctrine, resource trend, assembly, reserve, and assault readiness.

AI doctrine and difficulty are independent skirmish settings. Match seed `SkirmishSettings::aiSeed` keeps tactical choices reproducible.

