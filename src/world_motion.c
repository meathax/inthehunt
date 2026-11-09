/* World V33 05E9B, 05EA8, 05EB5, 05EC2. Each actor coordinate has
 * eight fractional bits followed by a little-endian whole-pixel word.
 * Velocity is signed 8.8; a negative subpixel velocity still contributes
 * an unsigned fractional byte and a negative whole-pixel displacement. */
typedef unsigned short Word;
typedef unsigned char Byte;
#pragma pack(push, 1)
typedef struct Axis {
    Byte fraction;
    Word pixels;
} Axis;
#pragma pack(pop)

/* The original far-call ABI exposes the V33 flags from the final ADC.
 * A byte-sum carry of 0 or 1 becomes CF before the integer addition. */
static Word add_pixels(Axis __far *axis, Word delta, Word carry);
#pragma aux add_pixels = "shr cx,1" "adc es:[di+1],ax" parm [es di] [ax] [cx] value [ax] modify exact [cx]

Word advance_axis(Axis __far *axis, Word velocity);
#pragma aux advance_axis parm [es di] [ax] value [ax] modify exact [ax]
Word advance_axis(Axis __far *axis, Word velocity)
{
    Word fraction = axis->fraction + (Byte)velocity;
    Word delta = (Word)(signed char)(velocity >> 8);
    axis->fraction = (Byte)fraction;
    return add_pixels(axis, delta, fraction >> 8);
}
