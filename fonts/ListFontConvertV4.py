import freetype
import argparse

# ==============================
# Parse Arguments
# ==============================
parser = argparse.ArgumentParser(description="Sparse MDI / custom font converter")
parser.add_argument("--font", required=True, help="Path to TTF font")
parser.add_argument("--size", type=int, required=True, help="Font size in pixels")
parser.add_argument("--threshold", type=int, default=70, help="Grayscale threshold (default=70)")
parser.add_argument("--output", default="SparseFont", help="Output font name")
parser.add_argument("--glyphs", help="Glyphs string (ex: abcd1234) or hex codes F0590,F0599")
parser.add_argument("--glyph-file", help="File with one glyph per line (hex or char)")

args = parser.parse_args()


# ==============================
# Load Glyph List
# ==============================
def load_glyphs():
    glyphs = set()
    s = args.glyphs
    buffer = ""
    escape = False

    for c in s:
        if escape:
            buffer += c
            escape = False
        elif c == "\\":
            escape = True
        elif c == ",":
            # end of entry
            if buffer:
                glyphs.update(parse_entry(buffer))
                buffer = ""
        else:
            buffer += c
    if buffer:
        glyphs.update(parse_entry(buffer))
    return sorted(glyphs)


def parse_entry(entry):
    result = set()
    entry = entry.strip()
    if not entry:
        return result
    # Hex code
    if entry.lower().startswith("0x"):
        try:
            result.add(int(entry, 16))
        except ValueError:
            print(f"Warning: invalid hex code '{entry}'")
    else:
        # literal characters
        for ch in entry:
            result.add(ord(ch))
    return result

# ==============================
# Converter
# ==============================
def convert_font():
    glyphs = load_glyphs()
    face = freetype.Face(args.font)
    face.set_pixel_sizes(0, args.size)

    bitmap_data = []
    glyph_entries = []
    char_table = []
    bitmap_offset = 0

    for codepoint in glyphs:
        try:
            face.load_char(chr(codepoint),
                           freetype.FT_LOAD_RENDER |
                           freetype.FT_LOAD_TARGET_NORMAL)
        except Exception as e:
            print(f"Warning: cannot load {hex(codepoint)} - {e}")
            continue

        glyph = face.glyph
        bitmap = glyph.bitmap

        width = bitmap.width
        height = bitmap.rows
        xAdvance = glyph.advance.x >> 6
        xOffset = glyph.bitmap_left
        yOffset = 1 - glyph.bitmap_top

        if width == 0 or height == 0:
            continue

        row_bytes = (width + 7) // 8

        # Convert grayscale → 1-bit manually
        for y in range(height):
            byte = 0
            bit = 0
            for x in range(width):
                pixel = bitmap.buffer[y * width + x]
                if pixel > args.threshold:
                    byte |= (0x80 >> bit)
                bit += 1
                if bit == 8:
                    bitmap_data.append(byte)
                    byte = 0
                    bit = 0
            if bit != 0:
                bitmap_data.append(byte)

        glyph_entries.append((bitmap_offset, width, height, xAdvance, xOffset, yOffset))
        char_table.append(codepoint)
        bitmap_offset += height * row_bytes

    write_header(bitmap_data, glyph_entries, char_table)


# ==============================
# Write .h file
# ==============================
def write_header(bitmap_data, glyph_entries, char_table):
    with open(f"{args.output}.h", "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        f.write("#include <Adafruit_GFX.h>\n\n")

        # Bitmaps
        f.write(f"const uint8_t {args.output}_Bitmaps[] PROGMEM = {{\n")
        for i, byte in enumerate(bitmap_data):
            f.write(f"0x{byte:02X},")
            if i % 12 == 11:
                f.write("\n")
        f.write("\n};\n\n")

        # Unicode / char map
        f.write(f"const uint32_t {args.output}_CharMap[] PROGMEM = {{\n")
        for c in char_table:
            f.write(f"0x{c:X},")
        f.write("\n};\n\n")

        # Glyph table
        f.write(f"const GFXglyph {args.output}_Glyphs[] PROGMEM = {{\n")
        for g in glyph_entries:
            f.write(f"{{ {g[0]}, {g[1]}, {g[2]}, {g[3]}, {g[4]}, {g[5]} }},\n")
        f.write("};\n\n")

#        f.write(
#            "struct SparseGFXfont {\n"
#            "  const uint8_t *bitmap;\n"
#            "  const GFXglyph *glyph;\n"
#            "  const uint32_t *charMap;\n"
#            "  uint16_t glyphCount;\n"
#            "  uint8_t yAdvance;\n"
#            "};\n\n"
#        )

        f.write(
            f"const SparseGFXfont {args.output} PROGMEM = {{\n"
            f"  {args.output}_Bitmaps,\n"
            f"  {args.output}_Glyphs,\n"
            f"  {args.output}_CharMap,\n"
            f"  {len(glyph_entries)},\n"
            f"  {args.size}\n"
            f"}};\n"
        )

    print(f"Font generated: {args.output}.h")
    print(f"Total glyphs: {len(char_table)}")
    print(f"Bitmap bytes: {len(bitmap_data)}")


# ==============================
# Main
# ==============================
if __name__ == "__main__":
    convert_font()
