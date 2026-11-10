#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
EDITOR_CPP="$ROOT_DIR/src/gui/editorwidget/editorwidget.cpp"
CODE_CPP="$ROOT_DIR/src/gui/editorwidget/codeeditor.cpp"

grep -F 'qApp->installEventFilter( this );' "$EDITOR_CPP" >/dev/null
grep -F 'QEvent::ShortcutOverride' "$EDITOR_CPP" >/dev/null
grep -F 'editor->isAncestorOf( widget )' "$EDITOR_CPP" >/dev/null
grep -F 'Qt::ControlModifier | Qt::MetaModifier' "$EDITOR_CPP" >/dev/null
grep -F 'if ( close() )' "$EDITOR_CPP" >/dev/null
grep -F 'event->key() == Qt::Key_W' "$CODE_CPP" >/dev/null
grep -F 'EditorWindow::self()->close()' "$CODE_CPP" >/dev/null
printf '%s\n' 'Editor shortcut contract PASS'
