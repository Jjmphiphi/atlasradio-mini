#include "cover_art.h"
#include <stddef.h>

void cover_art_request(const char *dir, const char *track)
{
    (void)dir;
    (void)track;
}

cover_emb_t cover_art_take_embedded(uint16_t **buf, int *w, int *h)
{
    if (buf) *buf = NULL;
    if (w) *w = 0;
    if (h) *h = 0;
    return COVER_EMB_NOTHING;
}

void cover_art_set_done_cb(void (*cb)(void))
{
    (void)cb;
}
