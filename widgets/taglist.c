/*
 * taglist.c - tag list widget
 *
 * Copyright © 2007 Aldo Cortesi <aldo@nullcube.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "client.h"
#include "widget.h"
#include "tag.h"
#include "lua.h"
#include "event.h"
#include "common/markup.h"
#include "common/configopts.h"

extern awesome_t globalconf;

typedef struct taglist_drawn_area_t taglist_drawn_area_t;
struct taglist_drawn_area_t
{
    void *object;
    area_t *area;
    taglist_drawn_area_t *next, *prev;
};

DO_SLIST(taglist_drawn_area_t, taglist_drawn_area, p_delete);

typedef struct
{
    char *text_normal, *text_focus, *text_urgent;
    bool show_empty;
    taglist_drawn_area_t *drawn_area;

} taglist_data_t;

static char *
tag_markup_parse(tag_t *t, const char *str, ssize_t len)
{
    const char *elements[] = { "title", NULL };
    char *title_esc = g_markup_escape_text(t->name, -1);
    const char *elements_sub[] = { title_esc , NULL };
    markup_parser_data_t *p;
    char *ret;

    p = markup_parser_data_new(elements, elements_sub, countof(elements));

    if(markup_parse(p, str, len))
    {
        ret = p->text;
        p->text = NULL;
    }
    else
        ret = a_strdup(str);

    markup_parser_data_delete(&p);
    p_delete(&title_esc);

    return ret;
}

/** Check if at least one client is tagged with tag number t and is on screen
 * screen
 * \param screen screen number
 * \param t tag
 * \return true or false
 */
static bool
tag_isoccupied(tag_t *t)
{
    client_t *c;

    for(c = globalconf.clients; c; c = c->next)
        if(is_client_tagged(c, t) && !c->skip)
            return true;

    return false;
}

static bool
tag_isurgent(tag_t *t)
{
    client_t *c;

    for(c = globalconf.clients; c; c = c->next)
        if(is_client_tagged(c, t) && c->isurgent)
            return true;

    return false;
}

static char *
taglist_text_get(tag_t *tag, taglist_data_t *data)
{
    if(tag->selected)
        return data->text_focus;
    else if(tag_isurgent(tag))
        return data->text_urgent;

    return data->text_normal;
}

static int
taglist_draw(draw_context_t *ctx, int screen, widget_node_t *w,
             int width, int height, int offset,
             int used __attribute__ ((unused)),
             void *object)
{
    tag_t *tag;
    taglist_data_t *data = w->widget->data;
    client_t *sel = globalconf.focus->client;
    screen_t *vscreen = &globalconf.screens[screen];
    int i = 0, prev_width = 0;
    area_t *area, rectangle = { 0, 0, 0, 0, NULL, NULL };
    char **text = NULL;
    taglist_drawn_area_t *tda;

    w->area.width = w->area.y = 0;

    /* Lookup for our taglist_drawn_area.
     * This will be used to store area where we draw tag list for each object.  */
    for(tda = data->drawn_area; tda && tda->object != object; tda = tda->next);

    /* Oh, we did not find a drawn area for our object. First time? */
    if(!tda)
    {
        /** \todo delete this when the widget is removed from the object */
        tda = p_new(taglist_drawn_area_t, 1);
        tda->object = object;
        taglist_drawn_area_list_push(&data->drawn_area, tda);
    }

    area_list_wipe(&tda->area);

    /* First compute text and widget width */
    for(tag = vscreen->tags; tag; tag = tag->next, i++)
    {
        p_realloc(&text, i + 1);
        area = p_new(area_t, 1);
        text[i] = taglist_text_get(tag, data);
        text[i] = tag_markup_parse(tag, text[i], a_strlen(text[i]));
        *area = draw_text_extents(ctx->connection, ctx->phys_screen,
                                  globalconf.font, text[i]);
        w->area.width += area->width;
        area_list_append(&tda->area, area);
    }

    /* Now that we have widget width we can compute widget x coordinate */
    w->area.x = widget_calculate_offset(width, w->area.width,
                                        offset, w->widget->align); 

    for(area = tda->area, tag = vscreen->tags, i = 0;
        tag && area;
        tag = tag->next, area = area->next, i++)
    {
        if (!data->show_empty && !tag->selected && !tag_isoccupied(tag))
            continue;

        area->x = w->area.x + prev_width;
        prev_width += area->width;
        draw_text(ctx, globalconf.font, *area, text[i]);
        p_delete(&text[i]);

        if(tag_isoccupied(tag))
        {
            rectangle.width = rectangle.height = (globalconf.font->height + 2) / 3;
            rectangle.x = area->x;
            rectangle.y = area->y;
            draw_rectangle(ctx, rectangle, 1.0,
                           sel && is_client_tagged(sel, tag), ctx->fg);
        }
    }

    p_delete(&text);

    w->area.height = height;
    return w->area.width;
}

static void
taglist_button_press(widget_node_t *w,
                     xcb_button_press_event_t *ev,
                     int screen,
                     void *object)
{
    screen_t *vscreen = &globalconf.screens[screen];
    button_t *b;
    taglist_data_t *data = w->widget->data;
    taglist_drawn_area_t *tda;
    area_t *area;
    tag_t *tag;

    /* Find the good drawn area list */
    for(tda = data->drawn_area; tda && tda->object != object; tda = tda->next);
    area = tda->area;

    for(b = w->widget->buttons; b; b = b->next)
        if(ev->detail == b->button && CLEANMASK(ev->state) == b->mod && b->fct)
            for(tag = vscreen->tags; tag && area; tag = tag->next, area = area->next)
                if(ev->event_x >= AREA_LEFT(*area)
                   && ev->event_x < AREA_RIGHT(*area))
                {
                    luaA_tag_userdata_new(tag);
                    luaA_dofunction(globalconf.L, b->fct, 1);
                    return;
                }
}

static widget_tell_status_t
taglist_tell(widget_t *widget, const char *property, const char *new_value)
{
    taglist_data_t *d = widget->data;

    if(!a_strcmp(property, "text_normal"))
    {
        p_delete(&d->text_normal);
        d->text_normal = a_strdup(new_value);
    }
    else if(!a_strcmp(property, "text_focus"))
    {
        p_delete(&d->text_focus);
        d->text_focus = a_strdup(new_value);
    }
    else if(!a_strcmp(property, "text_urgent"))
    {
        p_delete(&d->text_urgent);
        d->text_urgent = a_strdup(new_value);
    }
    else if(!a_strcmp(property, "show_empty"))
        d->show_empty = a_strtobool(new_value);
    else
        return WIDGET_ERROR;

    return WIDGET_NOERROR;
}

widget_t *
taglist_new(alignment_t align)
{
    widget_t *w;
    taglist_data_t *d;

    w = p_new(widget_t, 1);
    widget_common_new(w);
    w->align = align;
    w->draw = taglist_draw;
    w->button_press = taglist_button_press;
    w->tell = taglist_tell;

    w->data = d = p_new(taglist_data_t, 1);
    d->text_normal = a_strdup(" <text align=\"center\"/><title/> ");
    d->text_focus = a_strdup(" <text align=\"center\"/><title/> ");
    d->text_urgent = a_strdup(" <text align=\"center\"/><title/> ");
    d->show_empty = true;

    /* Set cache property */
    w->cache_flags = WIDGET_CACHE_TAGS | WIDGET_CACHE_CLIENTS;

    return w;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
