# HTTP Hyperlinks for Geany

## About

Underlines `http://` and `https://` links and makes them clickable with
Ctrl pressed. Holding Ctrl over a link shows a hand cursor.

## Build and install

Requires a C compiler, Make, pkg-config, and Geany development headers. On
Debian/Ubuntu these are supplied by `build-essential`, `pkg-config`, and
`libgeany-dev` and `libgtk-3-dev`. This project targets Geany 2.1.

```sh
make
make install
```

The default installation directory is `~/.config/geany/plugins`
For a custom Geany configuration directory, use
`make install PLUGIN_DIR=/path/to/config/plugins`.

Enable **HTTP Hyperlinks** in **Tools → Plugin Manager**.

To uninstall, disable it and remove `hyperlink.so` from that plugin directory.

## Behavior and limits

A link is opened using system default browser unless overridden by configured
browser in Geany preferences.

Scintilla supplies the underline indicators; a small scanner identifies URLs
without replacing syntax highlighting. Existing documents are scanned when
the plugin is enabled; edited lines are rescanned on insertion, deletion, undo,
and redo. Reloaded and newly opened documents are handled too.

URLs stop at whitespace, quotes, angle brackets, backticks, or unmatched closing
brackets. Balanced brackets inside URLs are preserved. Trailing sentence
punctuation (`.,;:!?`) is excluded. Only explicit HTTP(S) links are supported,
not bare domains or wrapped URLs.

The plugin uses Scintilla container indicator 20. If another installed plugin
uses that slot, rebuild with an unused container slot, for example
`make clean && make CPPFLAGS=-DLINK_INDICATOR=21`. Avoid Geany's slots 8 and 9,
lexer slots 0–7, and Scintilla's reserved slots 32 and above.

Reference: [Scintilla indicators](https://www.scintilla.org/ScintillaDoc.html#Indicators).

## License

Pluginis distributed under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.
