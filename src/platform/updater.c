/* updater.c — see updater.h. Optional libsoup3/libsoup2 backend; the fetch
 * runs on a worker thread and the result is delivered via g_idle_add. */

#include "updater.h"
#include "../util/log.h"

#include <stdlib.h>
#include <string.h>

#if defined(PNC_HAVE_SOUP3) || defined(PNC_HAVE_SOUP2)
#  include <libsoup/soup.h>
#  define PNC_HTTP 1
#else
#  define PNC_HTTP 0
#endif

typedef struct {
    char *slug;           /* owned: "user/repo" */
    char *current;        /* owned: version we are running */
    PncUpdateCallback cb;
    gpointer ud;
} UpdateCtx;

/* ── helpers shared by both paths ─────────────────────────────────── */

/* Extract the first "tag_name":"..." value from a GitHub releases JSON body.
 * Returns a newly allocated string or NULL. */
static char *
parse_tag_name (const char *body)
{
    if (!body) return NULL;
    const char *m = strstr (body, "\"tag_name\"");
    if (!m) return NULL;
    m = strchr (m, ':');
    if (!m) return NULL;
    m = strchr (m, '"');
    if (!m) return NULL;
    m++;
    const char *e = strchr (m, '"');
    if (!e) return NULL;
    return g_strndup (m, e - m);
}

/* Simple numeric-ish comparison: strips leading 'v' and compares dotted
 * integers. Returns TRUE if `latest` is newer than `current`. */
static gboolean
version_is_newer (const char *latest, const char *current)
{
    if (!latest) return FALSE;
    if (!current || !*current) return TRUE;
    const char *a = latest, *b = current;
    if (*a == 'v') a++;
    if (*b == 'v') b++;
    while (*a && *b) {
        long na = strtol (a, (char **) &a, 10);
        long nb = strtol (b, (char **) &b, 10);
        if (na != nb) return na > nb;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return *a != '\0';              /* latest has extra components */
}

/* Deliver result to the main thread. `result` may be NULL. */
typedef struct { PncUpdateCallback cb; gpointer ud; char *result; } IdleMsg;

static gboolean
deliver_idle (gpointer p)
{
    IdleMsg *m = p;
    m->cb (m->result, m->ud);
    g_free (m->result);
    g_free (m);
    return G_SOURCE_REMOVE;
}

static void
deliver (PncUpdateCallback cb, gpointer ud, char *result)
{
    IdleMsg *m = g_new0 (IdleMsg, 1);
    m->cb = cb; m->ud = ud; m->result = result;
    g_idle_add (deliver_idle, m);
}

#if PNC_HTTP
static gpointer
fetch_thread (gpointer p)
{
    UpdateCtx *ctx = p;
    char *body = NULL;
    SoupSession *sess = soup_session_new ();
    g_autofree char *uri =
        g_strdup_printf ("https://api.github.com/repos/%s/releases/latest", ctx->slug);

#if defined(PNC_HAVE_SOUP3)
    SoupMessage *msg = soup_message_new ("GET", uri);
    gsize len = 0;
    GBytes *bytes = soup_session_send_and_read (sess, msg, NULL, &len);
    if (bytes) {
        body = g_strndup (g_bytes_get_data (bytes, NULL), len);
        g_bytes_unref (bytes);
    }
    g_object_unref (msg);
#else  /* libsoup 2 */
    SoupMessage *msg = soup_message_new (SOUP_METHOD_GET, uri);
    guint status = soup_session_send_message (sess, msg);
    if (status == 200 && SOUP_MESSAGE_BODY_LENGTH (msg) > 0)
        body = g_strndup (msg->response_body->data,
                          msg->response_body->length);
    g_object_unref (msg);
#endif
    g_object_unref (sess);

    char *tag   = parse_tag_name (body);
    gboolean newer = version_is_newer (tag, ctx->current);
    g_free (body);

    LOG_INFO ("update check %s: latest=%s current=%s -> %s",
              ctx->slug, tag ? tag : "(none)", ctx->current,
              newer ? "UPDATE AVAILABLE" : "up to date");

    deliver (ctx->cb, ctx->ud, newer ? tag : (g_free (tag), NULL));
    g_free (ctx->slug);
    g_free (ctx->current);
    g_free (ctx);
    return NULL;
}
#endif /* PNC_HTTP */

void
pnc_update_check_async (const char *slug, const char *current_ver,
                        PncUpdateCallback cb, gpointer user_data)
{
    g_return_if_fail (slug && cb);

#if PNC_HTTP
    UpdateCtx *ctx = g_new0 (UpdateCtx, 1);
    ctx->slug    = g_strdup (slug);
    ctx->current = g_strdup (current_ver ? current_ver : "");
    ctx->cb      = cb;
    ctx->ud      = user_data;
    g_thread_unref (g_thread_new ("pnc-update", fetch_thread, ctx));
#else
    (void) current_ver;
    LOG_INFO ("update check: built without libsoup — skipping");
    deliver (cb, user_data, NULL);
#endif
}

gboolean
pnc_update_check_available (void)
{
#if PNC_HTTP
    return TRUE;
#else
    return FALSE;
#endif
}
