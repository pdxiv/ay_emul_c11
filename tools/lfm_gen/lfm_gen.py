#!/usr/bin/env python3
"""`.lfm`-to-C11/GTK2 generator - two modes.

Default mode: `.lfm`-to-C11/GTK2-skeleton generator - Phase 5 kickoff
proof of concept (see PORTING_TO_C11_LINUX.md §5.1). Parses a Lazarus
`.lfm` form file's plain-text nested `object`/property grammar and emits
a GTK2 C skeleton: one `gtk_fixed_new()` container positioned with the
`.lfm`'s literal Left/Top/Width/Height coordinates (the layout decision
recorded in the approved plan - GtkFixed, not GtkGrid/GtkBox, for this
milestone), one widget-creation call per child object keyed off its
Pascal type, and one signal-connect + matching empty handler function
per `On*` property found - stub bodies only, real dialog logic is later-
milestone work (see gui/dialogs/*.c's own file comments and
migration_debt.yaml). Usage: `lfm_gen.py <input.lfm> <output.c>`.

`--widgets-only` mode (MIG-0132): build-time widget-*construction* only
generation, for windows (like Mixer.pas) that already have a hand-tuned
idiomatic GTK2 layout deliberately NOT mirroring `.lfm`'s own literal
anchor-based coordinates - see gui/src/mixer_win.c's own header comment
for the full rationale. Emits a flat `struct { GtkWidget* <LfmName>; ...
}` (one field per named, constructible widget, field name = the `.lfm`
object's own name) plus one `<basename>_create(<struct>*)` function that
constructs every widget with its correct type/orientation/range/caption/
initial value/radio-grouping - and NOTHING else: no parenting
(gtk_container_add/gtk_box_pack_start/gtk_table_attach), no signal
wiring (g_signal_connect). Both stay 100% hand-written, in the caller,
exactly as before this mode existed - see the Context section of the
MIG-0132 migration_debt.yaml entry for why that split is deliberate.
Usage: `lfm_gen.py --widgets-only <input.lfm> <output_basename>` (writes
`<output_basename>.h` and `<output_basename>.c`).

Neither mode needs Lazarus/FPC RTTI: `.lfm` files are read here as plain
nested text, never loaded/executed.
"""
import re
import sys

OBJECT_RE = re.compile(r"^object\s+(\w+)\s*:\s*(\w+)\s*$")
PROP_RE = re.compile(r"^([\w.]+)\s*=\s*(.*)$")

# Pascal LCL type -> (GTK2 constructor expression, needs-caption-setter)
WIDGET_CTORS = {
    "TLabel": ('gtk_label_new("")', "gtk_label_set_text"),
    "TEdit": ('gtk_entry_new()', None),
    "TButton": ('gtk_button_new_with_label("")', "gtk_button_set_label"),
    "TProgressBar": ('gtk_progress_bar_new()', None),
}

# Pascal event property name -> plausible GTK2 signal name. Approximate
# mapping for skeleton purposes only - not all Pascal event semantics
# have a 1:1 GTK2 signal equivalent (e.g. form-level OnShow vs widget
# "show"); a human fills in the real wiring when a dialog is actually
# ported, this generator just avoids silently dropping the event name.
EVENT_SIGNAL_MAP = {
    "OnClick": "clicked",
    "OnChange": "changed",
    "OnShow": "show",
    "OnClose": "destroy",
}


class LfmObject:
    def __init__(self, name, typ):
        self.name = name
        self.typ = typ
        self.props = {}
        self.children = []

    def prop(self, key, default=None):
        return self.props.get(key, default)


def parse_object(lines, i):
    m = OBJECT_RE.match(lines[i].strip())
    assert m, "expected 'object Name: Type' at line %d: %r" % (i, lines[i])
    obj = LfmObject(m.group(1), m.group(2))
    i += 1
    while i < len(lines):
        line = lines[i].strip()
        if line == "end":
            return obj, i + 1
        if line.startswith("object "):
            child, i = parse_object(lines, i)
            obj.children.append(child)
            continue
        pm = PROP_RE.match(line)
        if pm:
            key, value = pm.group(1), pm.group(2).strip()
            if value == "(":
                # Multi-line list property (e.g. Items.Strings = ( ... ))
                # - consume until the closing ')' on its own line, same
                # grammar tools/lfm_analyze/lfm_analyze.py's own parser
                # handles.
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
    raise ValueError("unterminated object %s" % obj.name)


def parse_lfm(text):
    lines = text.splitlines()
    i = 0
    while i < len(lines) and not lines[i].strip():
        i += 1
    root, _ = parse_object(lines, i)
    return root


def unquote(pascal_str):
    """''-quoted Pascal string literal -> plain text (best-effort)."""
    s = pascal_str.strip()
    if s.startswith("'") and s.endswith("'"):
        return s[1:-1].replace("''", "'")
    return s


def c_ident(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def c_string_literal(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return '"%s"' % escaped


# ---------------------------------------------------------------------------
# Default mode: full self-contained GtkFixed skeleton (unchanged from the
# original Phase 5 kickoff generator).
# ---------------------------------------------------------------------------


def emit_widget_decl(obj):
    var = "w_" + c_ident(obj.name)
    return "  GtkWidget* %s;" % var


def emit_widget_init(obj, out_lines, handler_names):
    var = "w_" + c_ident(obj.name)
    ctor, caption_setter = WIDGET_CTORS.get(
        obj.typ, ('gtk_label_new("TODO: unmapped type %s")' % obj.typ, None))
    out_lines.append("  d->%s = %s;" % (var, ctor))

    caption = obj.props.get("Caption")
    if caption is not None and caption_setter:
        out_lines.append('  %s(GTK_%s(d->%s), "%s");' %
                          (caption_setter, obj.typ[1:].upper(), var,
                           unquote(caption)))

    left = obj.props.get("Left", "0")
    top = obj.props.get("Top", "0")
    width = obj.props.get("Width")
    height = obj.props.get("Height")
    if width and height:
        out_lines.append("  gtk_widget_set_size_request(d->%s, %s, %s);" %
                          (var, width, height))
    out_lines.append("  gtk_fixed_put(GTK_FIXED(d->fixed), d->%s, %s, %s);" %
                      (var, left, top))

    for prop, value in obj.props.items():
        if prop in EVENT_SIGNAL_MAP:
            handler_name = "on_%s_%s" % (c_ident(obj.name), c_ident(unquote(value)))
            signal = EVENT_SIGNAL_MAP[prop]
            out_lines.append(
                '  g_signal_connect(d->%s, "%s", G_CALLBACK(%s), d);' %
                (var, signal, handler_name))
            handler_names.append((handler_name, prop, obj.name))
    out_lines.append("")


def generate(root, source_name, struct_name):
    decls = []
    inits = []
    handler_names = []

    def walk(obj):
        decls.append(emit_widget_decl(obj))
        emit_widget_init(obj, inits, handler_names)
        for child in obj.children:
            walk(child)

    for child in root.children:
        walk(child)

    caption = unquote(root.props.get("Caption", root.name))
    width = root.props.get("Width", "300")
    height = root.props.get("Height", "200")

    lines = []
    lines.append("/* Generated by tools/lfm_gen/lfm_gen.py from %s - Phase 5" %
                  source_name)
    lines.append(" * kickoff proof-of-concept skeleton (see")
    lines.append(" * PORTING_TO_C11_LINUX.md §5.1 and migration_debt.yaml).")
    lines.append(" *")
    lines.append(" * GtkFixed layout with the .lfm's literal coordinates (the")
    lines.append(" * recorded layout decision for this milestone). Handler bodies")
    lines.append(" * below are STUBS ONLY - this dialog is not wired to real")
    lines.append(" * application logic yet; that is separate, later-milestone")
    lines.append(" * work, same as every other not-yet-ported dialog.")
    lines.append(" */")
    lines.append('#include <gtk/gtk.h>')
    lines.append('#include <stdbool.h>')
    lines.append("")
    lines.append("typedef struct %s {" % struct_name)
    lines.append("  GtkWidget* window;")
    lines.append("  GtkWidget* fixed;")
    for d in decls:
        lines.append(d)
    lines.append("} %s;" % struct_name)
    lines.append("")

    for handler_name, prop, owner in handler_names:
        lines.append("/* Stub for %s's %s - fill in real behavior when this" %
                      (owner, prop))
        lines.append(" * dialog is actually ported (not this milestone). */")
        lines.append("static void %s(GtkWidget* widget, gpointer data) {" %
                      handler_name)
        lines.append("  (void)widget;")
        lines.append("  (void)data;")
        lines.append('  g_printerr("%s: not yet implemented\\n");' % handler_name)
        lines.append("}")
        lines.append("")

    lines.append("bool %s_create(%s* d) {" % (struct_name, struct_name))
    lines.append("  d->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);")
    lines.append('  gtk_window_set_title(GTK_WINDOW(d->window), "%s");' % caption)
    lines.append("  gtk_widget_set_size_request(d->window, %s, %s);" %
                  (width, height))
    lines.append("  d->fixed = gtk_fixed_new();")
    lines.append("  gtk_container_add(GTK_CONTAINER(d->window), d->fixed);")
    lines.append("")
    for line in inits:
        lines.append(line)
    lines.append("  gtk_widget_show_all(d->window);")
    lines.append("  return true;")
    lines.append("}")
    lines.append("")
    lines.append("void %s_destroy(%s* d) {" % (struct_name, struct_name))
    lines.append("  gtk_widget_destroy(d->window);")
    lines.append("}")
    lines.append("")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# --widgets-only mode (MIG-0132): construction only, no parenting/signals.
# ---------------------------------------------------------------------------

# Pascal type -> GTK2 constructor expression template. `%(cap)s` is the
# already-quoted C string literal of Caption/Text if present, else '""'.
# Types not listed here (TBevel - purely decorative; TFrmMixer/TFrmXxx -
# the form/window itself, hand-written; anything else unrecognized) are
# skipped: no field, no construction, but children are still walked (a
# container type this generator doesn't yet know about shouldn't hide
# the real widgets inside it).
SIMPLE_WIDGETS_ONLY_CTORS = {
    "TGroupBox": 'gtk_frame_new(%(cap)s)',
    "TPanel": 'gtk_vbox_new(FALSE, 0)',
    "TLabel": 'gtk_label_new(%(cap)s)',
    "TEdit": 'gtk_entry_new()',
    "TCheckBox": 'gtk_check_button_new_with_label(%(cap)s)',
    "TButton": 'gtk_button_new_with_label(%(cap)s)',
    "TSpeedButton": 'gtk_button_new_with_label(%(cap)s)',
    "TComboBox": 'gtk_combo_box_text_new()',
    "TPageControl": 'gtk_notebook_new()',
    "TTabSheet": 'gtk_vbox_new(FALSE, 6)',
}
WIDGETS_ONLY_SKIP_TYPES = {"TBevel"}


def widgets_only_caption_literal(obj):
    cap = obj.prop("Caption")
    if cap is None:
        cap = obj.prop("Text")
    if cap is None or isinstance(cap, list):
        return '""'
    return c_string_literal(unquote(cap))


def widgets_only_emit_common_props(obj, fname, out):
    hint = obj.prop("Hint")
    if hint and not isinstance(hint, list):
        out.append("  gtk_widget_set_tooltip_text(g->%s, %s);" %
                    (fname, c_string_literal(unquote(hint))))
    if obj.prop("Checked") == "True":
        out.append(
            "  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->%s), TRUE);" %
            fname)
    if obj.typ == "TEdit":
        if obj.prop("ReadOnly") == "True":
            out.append("  gtk_editable_set_editable(GTK_EDITABLE(g->%s), FALSE);" %
                        fname)
        text = obj.prop("Text")
        if text is not None and not isinstance(text, list):
            out.append('  gtk_entry_set_text(GTK_ENTRY(g->%s), %s);' %
                        (fname, c_string_literal(unquote(text))))
    if obj.prop("Enabled") == "False":
        out.append("  gtk_widget_set_sensitive(g->%s, FALSE);" % fname)


def widgets_only_emit_trackbar(obj, fname, out):
    """Mixer.lfm's own TTrackBars have no `Orientation` property anywhere
    (trHorizontal is TTrackBar's own LCL default) - decided here purely
    from Width/Height, same heuristic a human should have applied by hand
    (and, this session, initially didn't - see gui/src/mixer_win.c's own
    header comment for the bug this generator mode exists to prevent)."""
    width = int(obj.prop("Width", "0") or 0)
    height = int(obj.prop("Height", "0") or 0)
    horizontal = width >= height
    min_v = obj.prop("Min", "0")
    # TTrackBar's own LCL default Max is 10 when the property is absent
    # (confirmed against Mixer.lfm's own TBNumBuf, which has no explicit
    # Max and this port's own hand-written equivalent already uses 10).
    max_v = obj.prop("Max", "10")
    pos = obj.prop("Position", min_v)
    ctor = "gtk_%sscale_new_with_range(%s.0, %s.0, 1.0)" % (
        "h" if horizontal else "v", min_v, max_v)
    out.append("  g->%s = %s;" % (fname, ctor))
    out.append("  gtk_range_set_value(GTK_RANGE(g->%s), %s.0);" % (fname, pos))
    widgets_only_emit_common_props(obj, fname, out)


def widgets_only_emit_radio(obj, fname, parent_key, radio_group_head, out):
    """Mixer.lfm has no explicit GroupIndex property anywhere - LCL/
    Delphi's own implicit rule (consecutive TRadioButtons under the same
    immediate parent form one group) is what every hand-written radio
    group in gui/src/mixer_win.c already replicates; this mirrors it."""
    cap = widgets_only_caption_literal(obj)
    head = radio_group_head.get(parent_key)
    if head is None:
        out.append("  g->%s = gtk_radio_button_new_with_label(NULL, %s);" %
                    (fname, cap))
        radio_group_head[parent_key] = fname
    else:
        out.append(
            "  g->%s = gtk_radio_button_new_with_label_from_widget("
            "GTK_RADIO_BUTTON(g->%s), %s);" % (fname, head, cap))
    if obj.prop("Checked") == "True":
        out.append(
            "  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->%s), TRUE);" %
            fname)
    hint = obj.prop("Hint")
    if hint and not isinstance(hint, list):
        out.append("  gtk_widget_set_tooltip_text(g->%s, %s);" %
                    (fname, c_string_literal(unquote(hint))))


def generate_widgets_only(root, source_name, basename):
    fields = []       # [(field_name, lfm_name, lfm_type)]
    creates = []       # C statements, in .lfm document order
    radio_group_head = {}  # id(parent LfmObject) -> field name

    def walk(obj, parent):
        if obj is not root:
            fname = c_ident(obj.name)
            if obj.typ in WIDGETS_ONLY_SKIP_TYPES:
                pass
            elif obj.typ == "TRadioButton":
                widgets_only_emit_radio(obj, fname, id(parent),
                                         radio_group_head, creates)
                fields.append((fname, obj.name, obj.typ))
            elif obj.typ == "TTrackBar":
                widgets_only_emit_trackbar(obj, fname, creates)
                fields.append((fname, obj.name, obj.typ))
            elif obj.typ in SIMPLE_WIDGETS_ONLY_CTORS:
                ctor = SIMPLE_WIDGETS_ONLY_CTORS[obj.typ] % {
                    "cap": widgets_only_caption_literal(obj)}
                creates.append("  g->%s = %s;" % (fname, ctor))
                widgets_only_emit_common_props(obj, fname, creates)
                fields.append((fname, obj.name, obj.typ))
            # else: unrecognized type - no field, no construction, but
            # still walk its children below (matches WIDGETS_ONLY_SKIP_
            # TYPES's own "don't hide real widgets inside it" rationale).
        for child in obj.children:
            walk(child, obj)

    walk(root, None)

    struct_name = basename
    create_fn = "%s_create" % basename

    h_lines = []
    h_lines.append("/* Generated by tools/lfm_gen/lfm_gen.py --widgets-only")
    h_lines.append(" * from %s (MIG-0132) - DO NOT EDIT, DO NOT COMMIT." %
                    source_name)
    h_lines.append(" *")
    h_lines.append(" * Widget CONSTRUCTION only: every field below is a real,")
    h_lines.append(" * unparented GtkWidget* with the correct type/orientation/")
    h_lines.append(" * range/caption/initial-value/radio-grouping straight from")
    h_lines.append(" * the .lfm this was generated from. Packing (gtk_box_pack_")
    h_lines.append(" * start/gtk_container_add/gtk_table_attach) and signal")
    h_lines.append(" * wiring (g_signal_connect) are NOT done here - see gui/src/")
    h_lines.append(" * mixer_win.c's own header comment for why both stay 100%%")
    h_lines.append(" * hand-written. Field names are the .lfm object's own name,")
    h_lines.append(" * verbatim, for 1:1 traceability back to the real source.")
    h_lines.append(" */")
    h_lines.append("#ifndef %s_H" % struct_name.upper())
    h_lines.append("#define %s_H" % struct_name.upper())
    h_lines.append("")
    h_lines.append("#include <gtk/gtk.h>")
    h_lines.append("")
    h_lines.append("typedef struct %s {" % struct_name)
    for fname, lfm_name, lfm_type in fields:
        h_lines.append("  GtkWidget* %s; /* %s: %s */" %
                        (fname, lfm_name, lfm_type))
    h_lines.append("} %s;" % struct_name)
    h_lines.append("")
    h_lines.append("void %s(%s* g);" % (create_fn, struct_name))
    h_lines.append("")
    h_lines.append("#endif")
    h_lines.append("")

    c_lines = []
    c_lines.append('/* Generated by tools/lfm_gen/lfm_gen.py --widgets-only')
    c_lines.append(' * from %s (MIG-0132) - DO NOT EDIT, DO NOT COMMIT. */' %
                    source_name)
    c_lines.append('#include "%s.h"' % basename)
    c_lines.append("")
    c_lines.append("void %s(%s* g) {" % (create_fn, struct_name))
    for line in creates:
        c_lines.append(line)
    c_lines.append("}")
    c_lines.append("")

    return "\n".join(h_lines), "\n".join(c_lines)


def main():
    args = sys.argv[1:]
    widgets_only = False
    if args and args[0] == "--widgets-only":
        widgets_only = True
        args = args[1:]

    if widgets_only:
        if len(args) != 2:
            sys.stderr.write(
                "usage: lfm_gen.py --widgets-only <input.lfm> "
                "<output_basename>\n")
            return 1
        in_path, out_basename = args
        with open(in_path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
        root = parse_lfm(text)
        basename = c_ident(out_basename.rsplit("/", 1)[-1])
        h_text, c_text = generate_widgets_only(root, in_path, basename)
        with open(out_basename + ".h", "w", encoding="utf-8") as f:
            f.write(h_text)
        with open(out_basename + ".c", "w", encoding="utf-8") as f:
            f.write(c_text)
        print("wrote %s.h and %s.c" % (out_basename, out_basename))
        return 0

    if len(args) != 2:
        sys.stderr.write("usage: lfm_gen.py <input.lfm> <output.c>\n")
        return 1
    in_path, out_path = args
    with open(in_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    root = parse_lfm(text)
    base = c_ident(root.name.lower().lstrip("frm").lstrip("_")) or "dlg"
    struct_name = "gui_" + base
    code = generate(root, in_path, struct_name)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(code)
    print("wrote %s (%d lines)" % (out_path, code.count("\n")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
