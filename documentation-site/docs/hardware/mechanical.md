# Scale platform and enclosure

The project has no reference enclosure: no printable case, no drawing, no hole pattern.
You build the base and the platform yourself.
This page lists what matters for weighing accurately and reading tags reliably.

## Mount the load cell

The YZC-133 is a bar that bends very slightly under load.
One end is fixed to the base, the other end carries the platform.
Use the mounting drawing that came with your cell for hole positions and screw sizes.

1. Find the fixed end, the load end and the arrow that shows the direction of force.
2. Screw the fixed end to a rigid base. Put a spacer between cell and base so the rest of the bar
   hangs free.
3. Screw the platform to the load end, again with a spacer.
   Do not clamp both ends to the same part.
4. Check that the platform touches nothing but the load cell: not the case, not a cable,
   not the NFC module. Press gently on each corner to be sure.
5. Fix the load-cell cable to the base and leave a relaxed loop towards the cell.
   A tight cable acts as a spring and shifts the zero point.
6. Make the platform large and level enough that a spool cannot tip or hang over an edge.

## Place the NFC reader

The antenna is on the NFC module itself.

- The tag on the spool must end up close to the module when the spool sits on the platform.
  Try the reading distance with your spool and tag before you fix anything in place.
- Keep metal away from the antenna and from the tag: the load cell, screws, metal plates.
  Metal next to the antenna can stop tags from being read.
- If the module is mounted on the moving platform, its cable must not pull on the platform.
- Test with the enclosure closed. A tag that reads on the open bench may not read in the case.

## Check your build

1. With the platform empty, [tare and calibrate](../scale/calibration.md).
2. Put the known weight on the middle of the platform, then near each edge.
   The readings should agree closely.
3. Remove the weight. The reading returns to zero.
4. Place a tagged spool the way you will use it. The station identifies it within a few seconds.

How accurate your scale is depends on your build.

## Overload

The station reports `Scale overload detected. Remove weight and retry.` when the weight is more
than the cell's capacity times the overload setting.
By default that is above 5500 g for the 5 kg cell and above 2200 g for the 2 kg cell.
The same message appears when the load-cell signal is out of range, whatever the weight,
for example with a loose or swapped cell wire.
This is only a message. It does not protect the cell: do not load it beyond its rated capacity.

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| The reading changes when you press on the case | The platform touches the case. | Find the contact point and make clearance. |
| Zero shifts when a cable moves | The cable pulls on the platform. | Fix the cable to the base and leave slack. |
| Different readings at the edges of the platform | The mounting is not rigid, or the load is off-centre. | Tighten the screws; stiffen base and platform. |
| The reading does not return to zero | Something binds, a screw is loose, or the cell was overloaded. | Check clearance and screws. |
| Tags stop reading once the case is closed | Metal near the antenna, or the tag is too far away. | Move the reader; test with the case closed. |

More: [Weight looks wrong](../troubleshooting/weight.md), [Tag is not detected](../troubleshooting/tag.md).
