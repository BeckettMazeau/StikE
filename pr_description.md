💡 **What:**
Optimized the DJB2-style hash loop used in `pushDirtySprite` to detect dirty rows. The loop uses 16-bit unrolling by 8 to avoid casting to 32-bit (which could cause strict aliasing undefined behavior and unaligned access faults). The loop bounds are precalculated (`w_fast = w & ~7`) to further minimize loop condition overhead.

🎯 **Why:**
Hash recalculation inside `DisplayManager::pushDirtySprite` scans every row on every draw loop when checking what to send to the display, slowing down UI rendering. Making this step faster reduces CPU cycles spent preparing TFT frames.

📊 **Measured Improvement:**
A dedicated C++ benchmark script (`benchmark18.cpp` / `benchmark19.cpp`) executing the `current_hash` and `optimized_hash` variants 100,000 times showed the following improvements:
- Baseline (no unroll): `4393 ms`
- Unroll 8, with pointer arithmetic/precalculation: `4266 ms - 4286 ms`

The performance boost yields a measurable CPU overhead reduction in sprite manipulation without breaking safe memory boundaries.
