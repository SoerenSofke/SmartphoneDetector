module sumlib;

import mir.ndslice.topology : iota, map;
import mir.algorithm.iteration : reduce;

extern (C) @nogc nothrow:

size_t sum_iota_doubled(size_t n)
{
    return size_t(0).reduce!"a + b"(iota(n).map!"a * 2");
}
