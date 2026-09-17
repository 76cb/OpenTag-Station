# Load cell, platform and enclosure

The project defines the supported load-cell profiles but does not provide an
enclosure CAD model, platform dimensions, hole spacing, screw lengths or a tested
mechanical bill of fasteners. Use the drawing supplied with your YZC-133 variant.
Do not drill a cell or choose screws from a guessed dimension.

## Mount a single-point load cell

Purpose: make all vertical spool force pass through the sensing beam without
friction or a second support path. You need a rigid base, platform, suitable
fasteners/spacers, the cell's mounting drawing and an unloaded work area.

1. Identify the fixed end, load end and force-direction marking from the cell
   drawing. Attach the fixed end to the rigid base using the specified mounting
   faces and fasteners; leave the sensing section free to flex.
2. Attach the platform to the load end. Use spacers as required by the actual
   cell geometry so the beam and platform have clearance over the full operating
   load range. Do not clamp both ends to the base.
3. Place the platform so normal spool force is vertical. Avoid twisting, lateral
   pulls and side-loading; prevent a tall spool from tipping off the platform.
4. Confirm the platform touches neither enclosure nor cable bundles. Check each
   corner gently while keeping load below rated capacity.
5. Fix the cell cable to the stationary structure with strain relief and a relaxed
   loop. A stretched cable can act as a spring and shift zero.
6. Keep the NFC antenna clear of the metal cell and fastening hardware. Test
   the intended spool/tag position before closing an enclosure.
7. Let the empty assembly settle, tare, then calibrate with a known reference mass.

Success: the unloaded zero is repeatable; the platform is free; centered loads
change raw counts smoothly; removing the reference restores zero. Repeat a known
mass at several platform positions and record the spread before trusting accuracy.

| Failure | Corrective action |
|---|---|
| Reading depends on pushing the enclosure | Find contact between moving platform and case |
| Zero changes when cable moves | Improve fixed-side strain relief and cable slack |
| Large position error | Check mounting rigidity, off-axis force and cell orientation |
| Reading sticks after unloading | Inspect binding, overload damage or loose fasteners |
| Tags stop reading after enclosure assembly | Move antenna away from metal; retest actual tag location |

The default software overload indication begins above 5,500 g for the 5 kg
profile (2,200 g for 2 kg). That is an error threshold, **not permission to exceed
the cell's rated capacity** or a physical overload stop. Mechanical stops, if
designed, must follow the cell manufacturer's allowable deflection and loads.
