#!/usr/bin/env python3
"""Turn a Bambu Studio command-line slice into a file Bambu Handy and the printer accept.

usage: package_sliced.py SLICED.3mf OUT.gcode.3mf [--thumbnails PREFIX] [--printer-model-id ID]

`bambu-studio --slice 0 --export-3mf` writes the plate gcode, but without a display it
makes no plate pictures and it leaves `printer_model_id` empty in slice_info.config.
This copies the archive entry for entry, adds PREFIX-plate_1.png, PREFIX-plate_1_small.png
and PREFIX-plate_no_light_1.png when they exist (render_thumbnails.py makes them), points
the plate metadata at them, and fills in the printer model id from Bambu's machine
profiles (P1S C12, X1 Carbon BL-P001, A1 N2S, A1 mini N1). The gcode and its md5 entry
are copied byte for byte and checked against each other.
"""
import argparse
import hashlib
import os
import re
import zipfile

PICTURES = {
    "plate_1.png": ("thumbnail_file", True),
    "plate_1_small.png": (None, False),
    "plate_no_light_1.png": ("thumbnail_no_light_file", False),
}
THUMBNAIL_RELS = (
    ('<Relationship Target="/Metadata/plate_1.png" Id="rel-2" '
     'Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail"/>'),
    ('<Relationship Target="/Metadata/plate_1.png" Id="rel-4" '
     'Type="http://schemas.bambulab.com/package/2021/cover-thumbnail-middle"/>'),
    ('<Relationship Target="/Metadata/plate_1_small.png" Id="rel-5" '
     'Type="http://schemas.bambulab.com/package/2021/cover-thumbnail-small"/>'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sliced")
    parser.add_argument("out")
    parser.add_argument("--thumbnails", metavar="PREFIX", help="PREFIX-plate_1.png etc. to embed")
    parser.add_argument("--printer-model-id", default="", help="Bambu model id, e.g. C12 for the P1S")
    args = parser.parse_args()

    pictures = {}
    if args.thumbnails:
        for name in PICTURES:
            path = f"{args.thumbnails}-{name}"
            if os.path.exists(path):
                with open(path, "rb") as f:
                    pictures[name] = f.read()
    missing = sorted(set(PICTURES) - set(pictures))
    if args.thumbnails and missing:
        print(f"  no preview for {', '.join(missing)}; packaging without")

    gcode_md5 = stored_md5 = None
    with zipfile.ZipFile(args.sliced) as zin, zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as zout:
        names = zin.namelist()
        if "Metadata/plate_1.gcode" not in names:
            raise SystemExit(f"{args.sliced} holds no plate gcode; slice it with --slice 0 first")
        for name in names:
            data = zin.read(name)
            if name == "Metadata/plate_1.gcode":
                gcode_md5 = hashlib.md5(data).hexdigest()
            elif name == "Metadata/plate_1.gcode.md5":
                stored_md5 = data.decode().strip().lower()
            elif name == "Metadata/model_settings.config" and pictures:
                data = add_plate_pictures(data.decode(), pictures).encode()
            elif name == "Metadata/slice_info.config" and args.printer_model_id:
                data = re.sub(r'(<metadata key="printer_model_id" value=")[^"]*(")',
                              rf"\g<1>{args.printer_model_id}\g<2>", data.decode(), count=1).encode()
            elif name == "_rels/.rels" and pictures:
                data = add_cover_rels(data.decode()).encode()
            elif name == "[Content_Types].xml" and pictures and 'Extension="png"' not in data.decode():
                data = data.decode().replace("</Types>", ' <Default Extension="png" ContentType="image/png"/>\n</Types>').encode()
            zout.writestr(zin.getinfo(name), data)
        for name, blob in pictures.items():
            if f"Metadata/{name}" not in names:
                zout.writestr(f"Metadata/{name}", blob)
    if stored_md5 is not None and gcode_md5 != stored_md5:
        raise SystemExit(f"gcode md5 {gcode_md5} does not match the stored {stored_md5}")
    print(f"  {os.path.basename(args.out)}: gcode md5 {gcode_md5}, {len(pictures)} previews, model id {args.printer_model_id or 'unset'}")


def add_plate_pictures(text, pictures):
    """Insert the thumbnail keys Bambu Studio writes before each plate's gcode_file entry."""
    if 'key="thumbnail_file"' in text:
        return text
    lines = []
    for name, (key, _) in PICTURES.items():
        if key and name in pictures:
            lines.append(f'    <metadata key="{key}" value="Metadata/{name}"/>\n')
    lines.append('    <metadata key="pattern_bbox_file" value="Metadata/plate_1.json"/>\n')
    return re.sub(r'(    <metadata key="gcode_file" value="Metadata/plate_1\.gcode"/>\n)',
                  lambda m: "".join(lines) + m.group(1), text, count=1)


def add_cover_rels(text):
    """Make sure the package relationships name the cover pictures."""
    extra = [rel for rel in THUMBNAIL_RELS if rel.split('"')[1] not in text]
    if not extra:
        return text
    return text.replace("</Relationships>", "".join(f" {rel}\n" for rel in extra) + "</Relationships>")


if __name__ == "__main__":
    main()
