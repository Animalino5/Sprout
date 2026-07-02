' lib/helpers.bas: Helper functions for the multi-file example

FUNCTION CLAMP_VAL(v, lo, hi)
    IF v < lo THEN RETURN lo
    IF v > hi THEN RETURN hi
    RETURN v
END FUNCTION

FUNCTION RANDOM_RANGE(min_val, max_val)
    RETURN min_val + RND(max_val - min_val)
END FUNCTION

FUNCTION DISTANCE(x1, y1, x2, y2)
    dx = x2 - x1
    dy = y2 - y1
    RETURN SQR(dx * dx + dy * dy)
END FUNCTION
