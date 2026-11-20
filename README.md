# Crescendo Engine v.0.9

A custom-built, low-level Vulkan graphics engine designed for developers who want absolute authority over their rendering pipeline. 
Your art, your way.

## Procedural Voxel Module
Voxels are currently in another test build that's not public, we will be bringing those features over once they are operational.

## RTS gameplay loop
[ still building this out so its not 100% implemented and is buggy. ] 
The RTS controller uses the same paid production and construction rules for both teams. Economy income advances once per simulated second; factories and engineers complete work over time instead of creating AI assets immediately.

Enemy command is split into a deterministic strategic planner and runtime execution:

- phases progress from bootstrap and fortification through expansion, staging, reconnaissance, assault, and recovery;
- utility-scored doctrines choose layered defense, chokepoint denial, mobile reserves, air denial, forward bastions, economic defense, or counterattacks;
- attacks require a stable economy, a staged mixed force, fresh intelligence, and a retained home reserve;
- difficulty changes reaction time and intelligence retention, not costs, income, or build speed;
- `F3` shows the active phase, doctrine, resource trend, assembly, reserve, and assault readiness.

AI doctrine and difficulty are independent skirmish settings. Match seed `SkirmishSettings::aiSeed` keeps tactical choices reproducible.

Future updates available in our discord : https://discord.gg/Qad36zEp4K developers interested in the project can also reach me there, right now we aren't actively looking for more devs but the opportunity is always welcome.

