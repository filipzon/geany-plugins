#include <geanyplugin.h>
#include <string.h>

typedef void (*UrlMatchFunc)(gsize start, gsize length, gpointer data);

static gboolean stop(guchar c)
{
    return c <= 32 || c == 127 || strchr("<>\"'`", c) != NULL;
}

static void url_match_foreach(const gchar *text, gsize length, UrlMatchFunc callback, gpointer data)
{
    for (gsize i = 0; i < length; i++) {
        gsize prefix = 0;
        if (i && (g_ascii_isalnum(text[i - 1]) || strchr("_+-.", text[i - 1])))
            continue;
        if (length - i >= 7 && g_ascii_strncasecmp(text + i, "http://", 7) == 0)
            prefix = 7;
        else if (length - i >= 8 && g_ascii_strncasecmp(text + i, "https://", 8) == 0)
            prefix = 8;
        if (!prefix)
            continue;
        gsize end = i + prefix;
        gint parens = 0, brackets = 0, braces = 0;
        while (end < length && !stop((guchar)text[end])) {
            gchar c = text[end];
            if (c == '(') parens++;
            if (c == '[') brackets++;
            if (c == '{') braces++;
            if (c == ')' && --parens < 0) break;
            if (c == ']' && --brackets < 0) break;
            if (c == '}' && --braces < 0) break;
            end++;
        }
        while (end > i + prefix && strchr(".,;:!?", text[end - 1])) end--;
        /* Require a nonempty authority, not just a scheme or a slash. */
        if (end > i + prefix && !strchr("/#?", text[i + prefix]))
            callback(i, end - i, data);
        if (end > i) i = end - 1;
    }
}

GeanyPlugin *geany_plugin;
GeanyData *geany_data;
PLUGIN_VERSION_CHECK(225)
PLUGIN_SET_INFO("HTTP Hyperlinks", "Underline HTTP URLs and open them with Ctrl+left-click.", "1.0", "Geany Hyperlink Plugin contributors")

/* Container indicator; can be overridden if another plugin uses this slot. */
#ifndef LINK_INDICATOR
#define LINK_INDICATOR 20
#endif

typedef struct { ScintillaObject *sci; sptr_t offset; } Paint;

static GdkWindow *hand_window;
static GdkCursor *previous_cursor;

static void text_window(GtkWidget *child, gpointer data)
{
    if (GTK_IS_DRAWING_AREA(child))
        *(GdkWindow **)data = gtk_widget_get_window(child);
}

static void reset_cursor(void)
{
    if (hand_window && !gdk_window_is_destroyed(hand_window))
        gdk_window_set_cursor(hand_window, previous_cursor);
    g_clear_object(&hand_window);
    g_clear_object(&previous_cursor);
}

/* Run after Scintilla, which also updates the cursor during mouse motion. */
static void cursor_event(GtkWidget *widget, GdkEvent *event, gpointer unused)
{
    (void)unused;
    GdkModifierType state = 0;
    gdk_event_get_state(event, &state);
    switch (event->type) {
    case GDK_MOTION_NOTIFY:
        if (!(state & GDK_CONTROL_MASK)) return;
        break;
    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
        if (event->key.keyval != GDK_KEY_Control_L &&
            event->key.keyval != GDK_KEY_Control_R) return;
        if (event->type == GDK_KEY_RELEASE) { reset_cursor(); return; }
        break;
    case GDK_LEAVE_NOTIFY:
        reset_cursor(); return;
    case GDK_FOCUS_CHANGE:
        if (!event->focus_change.in) reset_cursor();
        return;
    default:
        return;
    }
    gint x, y;
    GdkWindow *window;
    if (event->type == GDK_MOTION_NOTIFY) {
        window = event->motion.window;
        x = event->motion.x;
        y = event->motion.y;
    } else {
        GdkDevice *pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget)));
        window = gdk_device_get_window_at_position(pointer, &x, &y);
    }
    if (!window) { reset_cursor(); return; }
    gint wx, wy, sx, sy;
    gdk_window_get_origin(window, &wx, &wy);
    gdk_window_get_origin(gtk_widget_get_window(widget), &sx, &sy);
    x += wx - sx;
    y += wy - sy;
    sptr_t pos = scintilla_send_message(SCINTILLA(widget), SCI_POSITIONFROMPOINTCLOSE, x, y);
    if (!window || pos < 0 || !scintilla_send_message(SCINTILLA(widget), SCI_INDICATORVALUEAT, LINK_INDICATOR, pos)) {
        reset_cursor(); return;
    }
    /* The drawing area's cursor overrides the outer Scintilla window's. */
    gtk_container_forall(GTK_CONTAINER(widget), text_window, &window);
    if (!window) return;
    if (window != hand_window) {
        reset_cursor();
        hand_window = g_object_ref(window);
        previous_cursor = gdk_window_get_cursor(window);
        if (previous_cursor) g_object_ref(previous_cursor);
    }
    GdkCursor *hand = gdk_cursor_new_for_display(gtk_widget_get_display(widget), GDK_HAND2);
    gdk_window_set_cursor(window, hand);
    g_object_unref(hand);
}

static void paint_match(gsize start, gsize length, gpointer data)
{
    Paint *paint = data;
    scintilla_send_message(paint->sci, SCI_INDICATORFILLRANGE, paint->offset + start, length);
}

static void refresh(ScintillaObject *sci, sptr_t start, sptr_t end)
{
    sptr_t previous = scintilla_send_message(sci, SCI_GETINDICATORCURRENT, 0, 0);
    sptr_t value = scintilla_send_message(sci, SCI_GETINDICATORVALUE, 0, 0);
    gchar *text = g_malloc(end - start + 1);
    struct Sci_TextRange range = {{start, end}, text};
    scintilla_send_message(sci, SCI_GETTEXTRANGE, 0, (sptr_t)&range);
    scintilla_send_message(sci, SCI_SETINDICATORCURRENT, LINK_INDICATOR, 0);
    scintilla_send_message(sci, SCI_SETINDICATORVALUE, 1, 0);
    scintilla_send_message(sci, SCI_INDICATORCLEARRANGE, start, end - start);
    Paint paint = {sci, start};
    url_match_foreach(text, end - start, paint_match, &paint);
    scintilla_send_message(sci, SCI_SETINDICATORCURRENT, previous, 0);
    scintilla_send_message(sci, SCI_SETINDICATORVALUE, value, 0);
    g_free(text);
}

static gboolean clicked(GtkWidget *widget, GdkEvent *generic_event, gpointer unused)
{
    (void)unused;
    if (generic_event->type != GDK_BUTTON_PRESS) return FALSE;
    GdkEventButton *event = &generic_event->button;
    if (event->button != 1 ||
        !(event->state & GDK_CONTROL_MASK)) return FALSE;
    ScintillaObject *sci = SCINTILLA(widget);
    sptr_t pos = scintilla_send_message(sci, SCI_POSITIONFROMPOINTCLOSE, (uptr_t)event->x, (sptr_t)event->y);
    if (pos < 0 || !scintilla_send_message(sci, SCI_INDICATORVALUEAT, LINK_INDICATOR, pos))
        return FALSE;
    sptr_t start = scintilla_send_message(sci, SCI_INDICATORSTART, LINK_INDICATOR, pos);
    sptr_t end = scintilla_send_message(sci, SCI_INDICATOREND, LINK_INDICATOR, pos);
    gchar *url = g_malloc(end - start + 1);
    struct Sci_TextRange range = {{start, end}, url};
    scintilla_send_message(sci, SCI_GETTEXTRANGE, 0, (sptr_t)&range);
    utils_open_browser(url);
    g_free(url);
    return TRUE;
}

static void attach(GObject *object, GeanyDocument *doc, gpointer unused)
{
    (void)object; (void)unused;
    ScintillaObject *sci = doc->editor->sci;
    if (!g_object_get_data(G_OBJECT(sci), "http-hyperlinks-attached")) {
        /* GTK emits "event" before "button-press-event", where Geany
         * consumes Ctrl+click for go-to-definition. Handle links first. */
        plugin_signal_connect(geany_plugin, G_OBJECT(sci), "event", FALSE,
                              G_CALLBACK(clicked), NULL);
        plugin_signal_connect(geany_plugin, G_OBJECT(sci), "event-after", FALSE,
                              G_CALLBACK(cursor_event), NULL);
        g_object_set_data(G_OBJECT(sci), "http-hyperlinks-attached", GINT_TO_POINTER(1));
    }
    scintilla_send_message(sci, SCI_INDICSETSTYLE, LINK_INDICATOR, INDIC_PLAIN);
    scintilla_send_message(sci, SCI_INDICSETFORE, LINK_INDICATOR, 0xd08030);
    refresh(sci, 0, scintilla_send_message(sci, SCI_GETLENGTH, 0, 0));
}

static gboolean notified(GObject *object, GeanyEditor *editor, SCNotification *nt, gpointer unused)
{
    (void)object; (void)unused;
    if (nt->nmhdr.code == SCN_MODIFIED &&
        (nt->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
        ScintillaObject *sci = editor->sci;
        sptr_t first = scintilla_send_message(sci, SCI_LINEFROMPOSITION, nt->position, 0);
        sptr_t lastpos = nt->position + ((nt->modificationType & SC_MOD_INSERTTEXT) ? nt->length : 0);
        sptr_t last = scintilla_send_message(sci, SCI_LINEFROMPOSITION, lastpos, 0);
        sptr_t start = scintilla_send_message(sci, SCI_POSITIONFROMLINE, first, 0);
        sptr_t end = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, last, 0);
        refresh(sci, start, end);
    }
    return FALSE;
}

void plugin_init(GeanyData *data)
{
    (void)data;
    plugin_signal_connect(geany_plugin, NULL, "document-open", TRUE, G_CALLBACK(attach), NULL);
    plugin_signal_connect(geany_plugin, NULL, "document-new", TRUE, G_CALLBACK(attach), NULL);
    plugin_signal_connect(geany_plugin, NULL, "document-reload", TRUE, G_CALLBACK(attach), NULL);
    plugin_signal_connect(geany_plugin, NULL, "editor-notify", FALSE, G_CALLBACK(notified), NULL);
    guint i;
    foreach_document(i) attach(NULL, documents[i], NULL);
}

void plugin_cleanup(void)
{
    reset_cursor();
    guint i;
    foreach_document(i) {
        ScintillaObject *sci = documents[i]->editor->sci;
        sptr_t previous = scintilla_send_message(sci, SCI_GETINDICATORCURRENT, 0, 0);
        scintilla_send_message(sci, SCI_SETINDICATORCURRENT, LINK_INDICATOR, 0);
        scintilla_send_message(sci, SCI_INDICATORCLEARRANGE, 0, scintilla_send_message(sci, SCI_GETLENGTH, 0, 0));
        scintilla_send_message(sci, SCI_SETINDICATORCURRENT, previous, 0);
        g_object_set_data(G_OBJECT(sci), "http-hyperlinks-attached", NULL);
    }
}
