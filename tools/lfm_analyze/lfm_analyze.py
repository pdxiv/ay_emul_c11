#!/usr/bin/env python3
"""Ground-truth structural analysis of a Lazarus `.lfm` form + its `.pas`
unit - built after a C11 GUI port (gui/src/mixer_win.c) was found to be
"randomly wrong" about tab names/captions/scope because prior analysis
relied on a research agent's paraphrase instead of the real source. This
script reads the real `.lfm`/`.pas` files directly (plain-text parse, no
Lazarus/FPC involved) and reports every widget's name, type, parent
container, tab membership, on-screen geometry/anchors, key state
properties, and event-handler wiring (cross-referenced into the `.pas`
unit for the handler's first real statement) - so a person or another
agent can check a specific claim ("what tab is WOSheet?", "what does
RBSR48kClick actually call?") against ground truth in one command instead
of re-grepping the `.lfm`/`.pas` files by hand each time.

Usage:
    lfm_analyze.py <Form.lfm> [<Form.pas>] [--tab NAME] [--json OUT.json]

Examples:
    lfm_analyze.py ../../ay_emul/Mixer.lfm ../../ay_emul/Mixer.pas
    lfm_analyze.py ../../ay_emul/Mixer.lfm ../../ay_emul/Mixer.pas --tab WOSheet
    lfm_analyze.py ../../ay_emul/Tools.lfm ../../ay_emul/Tools.pas

Not specific to Mixer.pas - works on any `.lfm`/`.pas` pair in ay_emul/
(Tools.pas, PlayList.pas, MainWin.pas, ...) since the `.lfm` grammar and
the `procedure T<Form>.<Handler>` convention are the same everywhere.
"""
import argparse
import re
import sys

# ---------------------------------------------------------------------------
# .lfm parsing - plain nested-text grammar:
#   object Name: Type
#     Prop = Value
#     Prop.Sub = Value
#     Prop = (
#       'item1'
#       'item2'
#     )
#     object Child: ChildType
#       ...
#     end
#   end
# Read as text only (matches tools/lfm_gen/lfm_gen.py's own established
# approach) - no Lazarus/FPC toolchain involved.
# ---------------------------------------------------------------------------

OBJECT_RE = re.compile(r"^object\s+(\w+)\s*:\s*(\w+)\s*$")
PROP_RE = re.compile(r"^([\w.]+)\s*=\s*(.*)$")


class LfmObject:
    def __init__(self, name, typ, parent=None):
        self.name = name
        self.typ = typ
        self.props = {}
        self.children = []
        self.parent = parent

    def prop(self, key, default=None):
        return self.props.get(key, default)


def parse_object(lines, i, parent=None):
    m = OBJECT_RE.match(lines[i].strip())
    assert m, "expected 'object Name: Type' at line %d: %r" % (i + 1, lines[i])
    obj = LfmObject(m.group(1), m.group(2), parent)
    i += 1
    while i < len(lines):
        line = lines[i].strip()
        if line == "end":
            return obj, i + 1
        if line.startswith("object "):
            child, i = parse_object(lines, i, obj)
            obj.children.append(child)
            continue
        pm = PROP_RE.match(line)
        if pm:
            key, value = pm.group(1), pm.group(2).strip()
            if value == "(":
                items = []
                i += 1
                while i < len(lines) and lines[i].strip() != ")":
                    items.append(lines[i].strip())
                    i += 1
                obj.props[key] = items
                i += 1
                continue
            obj.props[key] = value
        i += 1
    raise ValueError("unterminated object %s (opened before line %d)" %
                      (obj.name, i))


def parse_lfm(text):
    lines = text.splitlines()
    i = 0
    while i < len(lines) and not lines[i].strip():
        i += 1
    root, _ = parse_object(lines, i)
    return root


def unquote(pascal_str):
    s = pascal_str.strip()
    if s.startswith("'") and s.endswith("'") and len(s) >= 2:
        return s[1:-1].replace("''", "'")
    return s


def iter_tree(obj):
    yield obj
    for c in obj.children:
        yield from iter_tree(c)


# ---------------------------------------------------------------------------
# .pas parsing - extract each `procedure T<Form>.<Handler>(...)` body so
# event-handler wiring found in the .lfm can show what the handler
# actually does (its first real statement), not just its name.
# ---------------------------------------------------------------------------

PROC_HEADER_RE = re.compile(
    r"^procedure\s+T\w+\.(\w+)\s*\(([^)]*)\)\s*;\s*$", re.MULTILINE)


def parse_pas_handlers(text):
    """Returns {handler_name: {'header': str, 'body_lines': [str, ...]}}."""
    handlers = {}
    matches = list(PROC_HEADER_RE.finditer(text))
    for idx, m in enumerate(matches):
        name = m.group(1)
        start = m.end()
        end = matches[idx + 1].start() if idx + 1 < len(matches) else len(text)
        block = text[start:end]
        # Body is between the first 'begin' and its matching final 'end;'
        # for this procedure - approximate via depth counting on
        # begin/end keywords (good enough for this codebase's style: one
        # begin/end pair per line, no begin..end on the same line as
        # other statements in the handlers we care about).
        lines = block.splitlines()
        body = []
        depth = 0
        started = False
        for line in lines:
            stripped = line.strip()
            if not started:
                if stripped == "begin":
                    started = True
                    depth = 1
                continue
            words = re.findall(r"\b(begin|end)\b", stripped)
            for w in words:
                if w == "begin":
                    depth += 1
                else:
                    depth -= 1
            if depth <= 0:
                break
            body.append(line.rstrip())
        line_no = text[:m.start()].count("\n") + 1
        handlers[name] = {
            "line": line_no,
            "params": m.group(2).strip(),
            "body_lines": body,
        }
    return handlers


def handler_summary(body_lines, max_lines=3):
    """First few non-comment, non-blank statement lines, for a quick
    'what does this actually do' preview without dumping the whole body."""
    out = []
    for line in body_lines:
        s = line.strip()
        if not s or s.startswith("//"):
            continue
        out.append(s)
        if len(out) >= max_lines:
            break
    return out


# ---------------------------------------------------------------------------
# Report generation
# ---------------------------------------------------------------------------

# Properties always worth showing when present (state that changes runtime
# behavior or visibility, not pure design-time layout noise).
INTERESTING_PROPS = [
    "Caption", "Text", "Hint",
    "Checked", "Enabled", "Visible", "ReadOnly", "TabStop",
    "TabOrder", "ItemIndex", "ImageIndex",
    "Min", "Max", "Position", "Frequency",
    "Style", "Alignment",
]

EVENT_PREFIX = "On"


def fmt_geom(obj):
    l = obj.prop("Left", "0")
    t = obj.prop("Top", "0")
    w = obj.prop("Width")
    h = obj.prop("Height")
    if w and h:
        return "(%s,%s) %sx%s" % (l, t, w, h)
    return "(%s,%s)" % (l, t)


def fmt_anchors(obj):
    sides = []
    for side in ("Left", "Top", "Right", "Bottom"):
        ctrl = obj.prop("AnchorSide%s.Control" % side)
        if ctrl is None:
            continue
        rel = obj.prop("AnchorSide%s.Side" % side)
        suffix = ""
        if rel:
            suffix = "." + rel.replace("asr", "").lower()
        sides.append("%s->%s%s" % (side[0], ctrl, suffix))
    return " ".join(sides)


def fmt_events(obj, handlers, show_body):
    lines = []
    for key, value in obj.props.items():
        if not key.startswith(EVENT_PREFIX) or "." in key:
            continue
        if not isinstance(value, str):
            continue
        h = handlers.get(value)
        if h is None:
            lines.append("    %s -> %s  [NOT FOUND in .pas]" % (key, value))
            continue
        lines.append("    %s -> %s()  [.pas:%d]" % (key, value, h["line"]))
        preview = handler_summary(h["body_lines"])
        for p in preview:
            lines.append("        %s" % p)
        if show_body and len(h["body_lines"]) > len(preview):
            lines.append("        ... (%d more lines)" %
                          (len(h["body_lines"]) - len(preview)))
    return lines


def fmt_props(obj):
    lines = []
    for key in INTERESTING_PROPS:
        if key in obj.props and "." not in key:
            v = obj.props[key]
            if isinstance(v, list):
                v = ", ".join(unquote(x) for x in v)
            else:
                v = unquote(v)
            lines.append("%s=%s" % (key, v))
    return lines


def render_tree(obj, handlers, show_body, indent=0, out=None):
    if out is None:
        out = []
    pad = "  " * indent
    props = fmt_props(obj)
    prop_str = (" " + " ".join(props)) if props else ""
    geom = fmt_geom(obj)
    out.append("%s%-14s %-22s %-24s%s" %
               (pad, obj.typ, obj.name, geom, prop_str))
    anchors = fmt_anchors(obj)
    if anchors:
        out.append("%s  anchors: %s" % (pad, anchors))
    for line in fmt_events(obj, handlers, show_body):
        out.append(pad + "  " + line.lstrip())
    for child in obj.children:
        render_tree(child, handlers, show_body, indent + 1, out)
    return out


def find_page_control(root):
    for obj in iter_tree(root):
        if obj.typ == "TPageControl":
            return obj
    return None


def build_flat_index(root):
    """name -> (type, tab_name_or_None, parent_chain_str)."""
    page_control = find_page_control(root)
    tab_names = set()
    if page_control:
        tab_names = {c.name for c in page_control.children if c.typ == "TTabSheet"}

    index = []
    for obj in iter_tree(root):
        if obj is root:
            continue
        chain = []
        p = obj.parent
        tab = None
        while p is not None:
            chain.append(p.name)
            if p.name in tab_names:
                tab = p.name
            p = p.parent
        index.append({
            "name": obj.name,
            "type": obj.typ,
            "tab": tab,
            "parent": obj.parent.name if obj.parent else None,
            "caption": unquote(obj.prop("Caption", "")) if obj.prop("Caption") else "",
        })
    return index


def print_report(root, handlers, args):
    print("=" * 88)
    caption = unquote(root.prop("Caption", root.name))
    print("FORM %s (%s)  Caption=%r  %sx%s" %
          (root.name, root.typ, caption,
           root.prop("Width", "?"), root.prop("Height", "?")))
    print("=" * 88)
    print()

    page_control = find_page_control(root)
    tabs = []
    if page_control:
        tabs = [c for c in page_control.children if c.typ == "TTabSheet"]
        print("%s '%s' - %d tab(s), real Pascal order:" %
              (page_control.typ, page_control.name, len(tabs)))
        for idx, t in enumerate(tabs):
            print("  [%d] %-16s Caption=%r" %
                  (idx, t.name, unquote(t.prop("Caption", ""))))
        print()

    selected = tabs
    if args.tab:
        selected = [t for t in tabs if t.name == args.tab]
        if not selected:
            sys.exit("no tab named %r (available: %s)" %
                      (args.tab, ", ".join(t.name for t in tabs)))

    if not tabs:
        # No TPageControl - just render everything under root.
        selected = [root]

    for t in selected:
        print("-" * 88)
        if t is root:
            print("(no TPageControl found - showing full form tree)")
        else:
            print("TAB %-16s Caption=%r  (ClientWidth=%s ClientHeight=%s)" %
                  (t.name, unquote(t.prop("Caption", "")),
                   t.prop("ClientWidth", "?"), t.prop("ClientHeight", "?")))
        print("-" * 88)
        for child in t.children:
            for line in render_tree(child, handlers, args.body):
                print(line)
        print()

    if args.index:
        print("=" * 88)
        print("FLAT WIDGET INDEX (alphabetical)")
        print("=" * 88)
        flat = build_flat_index(root)
        for entry in sorted(flat, key=lambda e: e["name"].lower()):
            tab = entry["tab"] or "-"
            cap = (" Caption=%r" % entry["caption"]) if entry["caption"] else ""
            print("  %-22s %-16s tab=%-14s parent=%-16s%s" %
                  (entry["name"], entry["type"], tab, entry["parent"], cap))


def write_json(root, handlers, path):
    import json

    def obj_to_dict(obj):
        d = {
            "name": obj.name,
            "type": obj.typ,
            "props": {k: v for k, v in obj.props.items()},
            "children": [obj_to_dict(c) for c in obj.children],
        }
        return d

    data = {
        "form": obj_to_dict(root),
        "handlers": {
            name: {"line": h["line"], "params": h["params"],
                   "body": h["body_lines"]}
            for name, h in handlers.items()
        },
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    print("wrote %s" % path, file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lfm", help="path to the .lfm form file")
    ap.add_argument("pas", nargs="?", default=None,
                     help="path to the matching .pas unit (default: same "
                          "basename as the .lfm, .pas extension)")
    ap.add_argument("--tab", default=None,
                     help="only report this one TTabSheet (by object name, "
                          "e.g. WOSheet)")
    ap.add_argument("--body", action="store_true",
                     help="note how many more lines a handler body has "
                          "beyond the short preview (still not a full dump "
                          "- use --json to get full bodies)")
    ap.add_argument("--index", action="store_true",
                     help="also print a flat alphabetical widget index "
                          "(name/type/tab/parent/caption)")
    ap.add_argument("--json", default=None,
                     help="also write the full parsed tree + handler "
                          "bodies as JSON to this path")
    args = ap.parse_args()

    pas_path = args.pas
    if pas_path is None:
        pas_path = re.sub(r"\.lfm$", ".pas", args.lfm)

    with open(args.lfm, "r", encoding="utf-8", errors="replace") as f:
        lfm_text = f.read()
    root = parse_lfm(lfm_text)

    handlers = {}
    try:
        with open(pas_path, "r", encoding="utf-8", errors="replace") as f:
            pas_text = f.read()
        handlers = parse_pas_handlers(pas_text)
    except FileNotFoundError:
        print("warning: %r not found - event handlers will show as "
              "'NOT FOUND in .pas'" % pas_path, file=sys.stderr)

    print_report(root, handlers, args)

    if args.json:
        write_json(root, handlers, args.json)


if __name__ == "__main__":
    main()
