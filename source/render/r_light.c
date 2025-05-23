/*
 * Copyright (C) 1996-1997 Id Software, Inc.
 * Copyright (C) 2007 Peter Mackay and Chris Swindle.
 * Copyright (C) 2010-2014 QuakeSpasm developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
// r_light.c -- Light updating and sampling

#include "../nzportable_def.h"

int r_dlightframecount;

// MARK: Dynamic Lights

/*
 * ==================
 * R_AnimateLight
 * ==================
 */
void
R_AnimateLight(void)
{
    int i, j, k;

    //
    // light animations
    // 'm' is normal light, 'a' is no light, 'z' is double bright
    i = (int) (cl.time * 10);
    for (j = 0 ; j < MAX_LIGHTSTYLES ; j++) {
        if (!cl_lightstyle[j].length) {
            d_lightstylevalue[j] = 256;
            continue;
        }
        k = i % cl_lightstyle[j].length;
        k = cl_lightstyle[j].map[k] - 'a';
        k = k * 22;
        d_lightstylevalue[j] = k;
    }
}

/*
 * =============
 * R_MarkLights -- johnfitz -- rewritten to use LordHavoc's lighting speedup
 * =============
 */
void
R_MarkLights(dlight_t * light, int num, mnode_t * node)
{
    mplane_t * splitplane;
    msurface_t * surf;
    vec3_t impact;
    float dist, l, maxdist;
    int i, j, s, t;

start:

    if (node->contents < 0)
        return;

    splitplane = node->plane;
    if (splitplane->type < 3)
        dist = light->origin[splitplane->type] - splitplane->dist;
    else
        dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;

    if (dist > light->radius) {
        node = node->children[0];
        goto start;
    }
    if (dist < -light->radius) {
        node = node->children[1];
        goto start;
    }

    maxdist = light->radius * light->radius;
    // mark the polygons
    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0 ; i < node->numsurfaces ; i++, surf++) {
        for (j = 0 ; j < 3 ; j++)
            impact[j] = light->origin[j] - surf->plane->normal[j] * dist;
        // clamp center of light to corner and check brightness
        l = DotProduct(impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0];
        s = l + 0.5f;
        s = bound(0, s, surf->extents[0]);
        s = l - s;
        l = DotProduct(impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1];
        t = l + 0.5f;
        t = bound(0, t, surf->extents[1]);
        t = l - t;
        // compare to minimum light
        if ((s * s + t * t + dist * dist) < maxdist) {
            if (surf->dlightframe != r_dlightframecount) { // not dynamic until now
                surf->dlightbits  = num;
                surf->dlightframe = r_dlightframecount;
            } else { // already dynamic
                surf->dlightbits |= num;
            }
        }
    }

    if (node->children[0]->contents >= 0)
        R_MarkLights(light, num, node->children[0]);
    if (node->children[1]->contents >= 0)
        R_MarkLights(light, num, node->children[1]);
} /* R_MarkLights */

/*
 * =============
 * R_PushDlights
 * =============
 */
void
R_PushDlights(void)
{
    int i;
    dlight_t * l;

    r_dlightframecount = r_framecount + 1; // because the count hasn't
                                           //  advanced yet for this frame
    l = cl_dlights;

    for (i = 0 ; i < MAX_DLIGHTS ; i++, l++) {
        if (l->die < (float) cl.time || !l->radius)
            continue;
        R_MarkLights(l, 1 << i, cl.worldmodel->nodes);
    }
}

// MARK: Light Sampling

mplane_t * lightplane;
vec3_t lightspot;

// LordHavoc: .lit support begin
// LordHavoc: original code replaced entirely
int
RecursiveLightPoint(vec3_t color, mnode_t * node, vec3_t start, vec3_t end)
{
    float front, back, frac;
    vec3_t mid;

loc0:
    if (node->contents < 0)
        return false;  // didn't hit anything

    // calculate mid point
    if (node->plane->type < 3) {
        front = start[node->plane->type] - node->plane->dist;
        back  = end[node->plane->type] - node->plane->dist;
    } else {
        front = DotProduct(start, node->plane->normal) - node->plane->dist;
        back  = DotProduct(end, node->plane->normal) - node->plane->dist;
    }

    // LordHavoc: optimized recursion
    if ((back < 0) == (front < 0)) {
        //		return RecursiveLightPoint (color, node->children[front < 0], start, end);
        node = node->children[front < 0];
        goto loc0;
    }

    frac   = front / (front - back);
    mid[0] = start[0] + (end[0] - start[0]) * frac;
    mid[1] = start[1] + (end[1] - start[1]) * frac;
    mid[2] = start[2] + (end[2] - start[2]) * frac;

    // go down front side
    if (RecursiveLightPoint(color, node->children[front < 0], start, mid))
        return true;  // hit something
    else {
        int i, ds, dt;
        msurface_t * surf;
        // check for impact on this node
        VectorCopy(mid, lightspot);
        lightplane = node->plane;

        surf = cl.worldmodel->surfaces + node->firstsurface;
        for (i = 0; i < node->numsurfaces; i++, surf++) {
            if (surf->flags & SURF_DRAWTILED)
                continue;  // no lightmaps

            ds = (int) ((float) DotProduct(mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
            dt = (int) ((float) DotProduct(mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

            if (ds < surf->texturemins[0] || dt < surf->texturemins[1])
                continue;

            ds -= surf->texturemins[0];
            dt -= surf->texturemins[1];

            if (ds > surf->extents[0] || dt > surf->extents[1])
                continue;

            if (surf->samples) {
                // LordHavoc: enhanced to interpolate lighting
                byte * lightmap;
                int maps, line3, dsfrac = ds & 15, dtfrac = dt & 15, r00 = 0, g00 = 0, b00 = 0, r01 = 0, g01 = 0,
                  b01 = 0, r10 = 0, g10 = 0, b10 = 0, r11 = 0, g11 = 0, b11 = 0;
                float scale;
                line3 = ((surf->extents[0] >> 4) + 1) * 3;

                lightmap = surf->samples + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3; // LordHavoc: *3 for color

                for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) {
                    scale     = (float) d_lightstylevalue[surf->styles[maps]] * 1.0f / 256.0f;
                    r00      += (float) lightmap[      0] * scale;
                    g00      += (float) lightmap[      1] * scale;
                    b00      += (float) lightmap[2] * scale;
                    r01      += (float) lightmap[      3] * scale;
                    g01      += (float) lightmap[      4] * scale;
                    b01      += (float) lightmap[5] * scale;
                    r10      += (float) lightmap[line3 + 0] * scale;
                    g10      += (float) lightmap[line3 + 1] * scale;
                    b10      += (float) lightmap[line3 + 2] * scale;
                    r11      += (float) lightmap[line3 + 3] * scale;
                    g11      += (float) lightmap[line3 + 4] * scale;
                    b11      += (float) lightmap[line3 + 5] * scale;
                    lightmap += ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1) * 3; // LordHavoc: *3 for colored lighting
                }

                color[0] += (float) ((int) ((((((((r11 - r10) * dsfrac) >> 4) + r10) - ((((r01 - r00) * dsfrac) >>
                  4) + r00)) * dtfrac) >> 4) + ((((r01 - r00) * dsfrac) >> 4) + r00)));
                color[1] += (float) ((int) ((((((((g11 - g10) * dsfrac) >> 4) + g10) - ((((g01 - g00) * dsfrac) >>
                  4) + g00)) * dtfrac) >> 4) + ((((g01 - g00) * dsfrac) >> 4) + g00)));
                color[2] += (float) ((int) ((((((((b11 - b10) * dsfrac) >> 4) + b10) - ((((b01 - b00) * dsfrac) >>
                  4) + b00)) * dtfrac) >> 4) + ((((b01 - b00) * dsfrac) >> 4) + b00)));
            }
            return true; // success
        }

        // go down back side
        return RecursiveLightPoint(color, node->children[front >= 0], mid, end);
    }
} /* RecursiveLightPoint */

vec3_t lightcolor; // LordHavoc: used by model rendering
int
R_LightPoint(vec3_t p)
{
    vec3_t end;

    if (r_fullbright.value || !cl.worldmodel->lightdata) {
        lightcolor[0] = lightcolor[1] = lightcolor[2] = 255;
        return 255;
    }

    end[0] = p[0];
    end[1] = p[1];
    end[2] = p[2] - 2048;

    lightcolor[0] = lightcolor[1] = lightcolor[2] = 0;
    RecursiveLightPoint(lightcolor, cl.worldmodel->nodes, p, end);
    return ((lightcolor[0] + lightcolor[1] + lightcolor[2]) * (1.0f / 3.0f));
}

// LordHavoc: .lit support end
