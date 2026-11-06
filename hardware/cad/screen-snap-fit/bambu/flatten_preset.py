"""Flatten a Bambu Studio preset by resolving its ``inherits`` chain and ``include`` list.

usage: flatten_preset.py <preset.json> <profiles-dir> <out.json> [compatible-printer]

``profiles-dir`` is Bambu Studio's ``resources/profiles/BBL`` directory, which holds the
``machine``, ``process`` and ``filament`` preset folders.

The Bambu Studio command line loads a preset file as written: it does not pull in the
parents named by ``inherits`` or the g-code templates named by ``include``. A project
exported from an unflattened system preset therefore carries slicer defaults (a 200 mm
bed, generic start g-code, slow speeds) instead of the printer's tuned profile. This
script resolves both so the exported project carries the complete profile.

Precedence, lowest to highest: parents, included files, the preset's own keys.
``inherits`` and ``include`` are dropped from the result. When a compatible-printer name
is given, ``compatible_printers`` is set to just that printer; the command line refuses a
user process preset that does not list the printer.
"""

import json
import os
import sys

DROP = ("inherits", "include")


def load_chain(path, root):
    preset = json.load(open(path))
    kind = preset["type"]
    result = {}
    parent = preset.get("inherits")
    if parent:
        parent_path = os.path.join(root, kind, parent + ".json")
        if not os.path.exists(parent_path):
            raise SystemExit("parent preset not found: %s" % parent_path)
        result.update(load_chain(parent_path, root))
    for name in preset.get("include", []):
        include_path = os.path.join(root, kind, name + ".json")
        if not os.path.exists(include_path):
            raise SystemExit("included preset not found: %s" % include_path)
        included = json.load(open(include_path))
        result.update({k: v for k, v in included.items() if k not in ("name", "instantiation") + DROP})
    result.update({k: v for k, v in preset.items() if k not in DROP})
    return result


def main():
    if len(sys.argv) not in (4, 5):
        raise SystemExit(__doc__)
    src, root, out = sys.argv[1:4]
    flat = load_chain(src, root)
    if len(sys.argv) == 5:
        flat["compatible_printers"] = [sys.argv[4]]
    with open(out, "w") as fh:
        json.dump(flat, fh, indent=4)
    print("%s: %d keys from %s" % (os.path.basename(out), len(flat), os.path.basename(src)))


if __name__ == "__main__":
    main()
