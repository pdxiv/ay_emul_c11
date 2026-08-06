#!/usr/bin/env python3
"""`.lfm`-to-C11/GTK2-skeleton generator - Phase 5 kickoff proof of concept.

Parses a Lazarus `.lfm` form file's plain-text nested `object`/property
grammar (see PORTING_TO_C11_LINUX.md §5.1) and emits a GTK2 C skeleton:
one `gtk_fixed_new()` container positioned with the `.lfm`'s literal
Left/Top/Width/Height coordinates (the layout decision recorded in the
approved plan - GtkFixed, not GtkGrid/GtkBox, for this milestone), one
widget-creation call per child object keyed off its Pascal type, and one
signal-connect + matching empty handler function per `On*` property
found - stub bodies only, real dialog logic is later-milestone work
(see gui/dialogs/*.c's own file comments and migration_debt.yaml).

This generator does not need Lazarus RTTI: `.lfm` files are read here as
plain nested text, not loaded/executed.

Usage: lfm_gen.py <input.lfm> <output.c>
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
            obj.props[pm.group(1)] = pm.group(2).strip()
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


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: lfm_gen.py <input.lfm> <output.c>\n")
        return 1
    in_path, out_path = sys.argv[1], sys.argv[2]
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
