/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2007 Peter Mackay and Chris Swindle.
Copyright (C) 2008-2009 Crow_bar.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

extern "C"
{
#include "../../../nzportable_def.h"
}
#include <malloc.h>
#include <pspgu.h>

#include "gu_fullbright.h"
#include "gu_images.h"

int LIGHTMAP_BYTES;

model_t	*loadmodel;
char	loadname[32];	// for hunk tags

//void UnloadWads (void);  //wad unload by Crow_bar

void Mod_LoadSpriteModel  (model_t *mod, void *buffer);
void Mod_LoadBrushModel   (model_t *mod, void *buffer);
void Mod_LoadAliasModel   (model_t *mod, void *buffer);
qboolean Mod_LoadQ2SpriteModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel    (model_t *mod, qboolean crash);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	512
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

//model_t	mod_inline[MAX_MOD_KNOWN];

cvar_t gl_subdivide_size = {"gl_subdivide_size", "128", qtrue};

extern int solidskytexture;
extern int alphaskytexture;

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&gl_subdivide_size);
	memset (mod_novis, 0xff, sizeof(mod_novis));
}

/*
===============
Mod_Init

Caches the data if needed
===============
*/
void *Mod_Extradata (model_t *mod)
{
	void	*r;

	r = Cache_Check (&mod->cache);
	if (r)
		return r;

	Mod_LoadModel (mod, qtrue);

	if (!mod->cache.data)
		Sys_Error ("caching failed");
	return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;

	if (!model || !model->nodes)
		Sys_Error ("bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->numleafs+7)>>3;
	out = decompressed;

#if 0
	memcpy (out, in, row);
#else
	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
#endif

	return decompressed;
}

byte *Mod_LeafPVS (mleaf_t *leaf, model_t *model)
{
	if (leaf == model->leafs)
		return mod_novis;
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

/*
===================
Mod_ClearAll
===================
*/
static byte *ent_file = NULL;
void Mod_ClearAll (void)
{
	int		i;
	model_t	*mod;

	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{

		if (mod->type != mod_alias)
		{
			mod->needload = qtrue;
        }

		//Models & Sprite Unloading code By Crow_bar
		if (mod->type == mod_alias)
		{
			if (Cache_Check (&mod->cache))
				Cache_Free (&mod->cache);
		}
		else if (mod->type == mod_sprite)
		{
		   mod->cache.data = NULL;
		}

	}

	ent_file = NULL; //~~~~

	GL_UnloadAllTextures();

	solidskytexture	= -1;
	alphaskytexture	= -1;

	//purge old sky textures
	for (i=0; i<5; i++)
		skyimage[i] = 0;

	//purge old lightmaps
	for (i=0; i<MAX_LIGHTMAPS; i++)
		lightmap_index[i] = 0;
}

/*
==================
Mod_FindName

==================
*/
model_t *Mod_FindName (char *name)
{
	int		i;
	model_t	*mod;

	if (!name[0])
		Sys_Error ("NULL name");

/*
	if (!name[0])
	{
		//Search a loaded model
		for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
			if (!strcmp (mod->name, name) )
				break;

		return mod;
	}
*/
//
// search the currently loaded models
//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (!strcmp (mod->name, name) )
			break;

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		strcpy (mod->name, name);
		mod->needload = qtrue;
		mod_numknown++;
	}

	return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (char *name)
{
	model_t	*mod;

	mod = Mod_FindName (name);

	if (!mod->needload && (mod->type == mod_alias))
		Cache_Check (&mod->cache);

}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t *Mod_LoadModel (model_t *mod, qboolean crash)
{
	void	*d;
	unsigned *buf;
	byte	stackbuf[1024];		// avoid dirtying the cache heap
    char		strip[128];
	char		md3name[132];

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
		{
			d = Cache_Check (&mod->cache);
			if (d)
				return mod;
		}
		else
			return mod;		// not cached at all
	}


//
// because the world is so huge, load it one piece at a time
//
	if (!crash)
	{

	}

//
// load the file
//
    if (r_loadq3models.value)
	{
		COM_StripExtension(mod->name, &strip[0]);
		snprintf (&md3name[0], 132, "%s.md3", &strip[0]);

		buf = (unsigned *)COM_LoadStackFile (md3name, stackbuf, sizeof(stackbuf));
		if (!buf)
		{
			buf = (unsigned *)COM_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf));
			if (!buf)
			{
				if (crash)
					Sys_Error ("%s not found", mod->name);
				return NULL;
			}
		}
	}
	else
	{
		buf = (unsigned *)COM_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf));
	    if (!buf && crash)
		{
			// Reload with another .mdl
			buf = (unsigned *)COM_LoadStackFile("models/missing_model.mdl", stackbuf, sizeof(stackbuf));
			if (buf)
			{
				Con_Printf ("Missing model %s substituted\n", mod->name);
			}
		}
		if (!buf)
		{
			if (crash)
				Sys_Error ("%s not found", mod->name);
			return NULL;
		}
	}
//
// allocate a new model
//
	COM_FileBase (mod->name, loadname);

	loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
	mod->needload = qfalse;

	switch (LittleLong(*(unsigned *)buf))
	{
	case IDPOLYHEADER:     //Quake .mdl support
		Mod_LoadAliasModel (mod, buf);
		break;
	case IDSPRITEHEADER:    //Quake      .spr .spr32 and HL sprite support
		Mod_LoadSpriteModel (mod, buf);
		break;
	case IDSPRITE2HEADER:   //Quake II  .sp2 support
		Mod_LoadQ2SpriteModel (mod, buf);
		break;
	default:               //.bsp ver 29,30 support
		Mod_LoadBrushModel (mod, buf);
		break;
	}

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t	*mod;
	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;
int GL_LoadTexturePixels (byte *data, char *identifier, int width, int height, int mode);

#define ISSKYTEX(name)		((name)[0] == 's' && (name)[1] == 'k' && (name)[2] == 'y')

#define ISTURBTEX(name)		((loadmodel->bspversion == BSPVERSION && (name)[0] == '*') ||	\
							 (loadmodel->bspversion == HL_BSPVERSION && (name)[0] == '!')

extern int nonetexture;
/*
=================
Mod_LoadTextures
=================
*/
void Mod_LoadTextures (lump_t *l)
{
	int		i, j, pixels, num, max, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t *m;
	char		texname[64];

    loadmodel->textures = NULL;

	char fbr_mask_name[64];

	if (!l->filelen)
	{
		return;
	}
	m = (dmiptexlump_t *)(mod_base + l->fileofs);

	m->nummiptex = LittleLong (m->nummiptex);

	loadmodel->numtextures = m->nummiptex;
	loadmodel->textures = static_cast<texture_t**>(Hunk_AllocName (m->nummiptex * sizeof(*loadmodel->textures) , loadname));

	loading_num_step = loading_num_step + m->nummiptex;

	for (i=0 ; i<m->nummiptex ; i++)
	{
		m->dataofs[i] = LittleLong(m->dataofs[i]);
		if (m->dataofs[i] == -1)
			continue;
		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		mt->width = LittleLong (mt->width);
		mt->height = LittleLong (mt->height);
		for (j=0 ; j<MIPLEVELS ; j++)
			mt->offsets[j] = LittleLong (mt->offsets[j]);

		if ( (mt->width & 15) || (mt->height & 15) )
			Sys_Error ("Texture %s is not 16 aligned", mt->name);

		pixels = mt->width*mt->height/64*85;

		tx = static_cast<texture_t*>(Hunk_AllocName (sizeof(texture_t) , loadname ));

		const std::size_t buffer_size = pixels;

        byte* tx_pixels = static_cast<byte*>(memalign(16, buffer_size));

		if (!tx_pixels)
		{
			Sys_Error("BrushTex: Out of RAM for loading textures\n");
		}
		loadmodel->textures[i] = tx;


		memcpy_vfpu(tx->name, mt->name, sizeof(tx->name));
		tx->width = mt->width;
		tx->height = mt->height;
		for (j=0 ; j<MIPLEVELS ; j++)
			tx->offsets[j] = mt->offsets[j] + sizeof(texture_t) - sizeof(miptex_t);
		// the pixels immediately follow the structures
		memcpy_vfpu( tx_pixels, mt+1, pixels);

		int level = 0;
		if (r_mipmaps.value > 0)
			level = 3;

    //if (loadmodel->isworldmodel && loadmodel->bspversion != HL_BSPVERSION && ISSKYTEX(tx->name))
    if (loadmodel->bspversion != HL_BSPVERSION)
	{
		R_InitSky (tx_pixels);
 	}
	else
	{
		if (loadmodel->bspversion == HL_BSPVERSION)
		{		
			char filename[128];		// Filename to check r4w file
			byte *data;
			snprintf(filename, 128, "textures/maps/%s/%s.r4w", sv.name, mt->name);		// search in textures/maps/MAPNAME/TEXNAME
			
			data = static_cast<byte*>(COM_LoadHunkFile(filename));
			
			if (data == NULL) {
				sprintf(filename, "textures/%s.r4w", mt->name);					// search in textures/TEXNAME
				data = static_cast<byte*>(COM_LoadHunkFile(filename));
			}
			
			if (data == NULL) {
				Con_Printf("Loading texture %s as WAD3, %dx%d\n", mt->name, mt->width, mt->height);		// didn't find the texture in the folder
					
				int index = WAD3_LoadTextureClut4(mt);

				if (index != 0) {
					tx->gl_texturenum = index;
					tx->fullbright = -1;
					tx->dt_texturenum = 0;
				} else {
					// Fall back to missing texture.
					Con_Printf("Texture %s not found\n", mt->name);
					tx->gl_texturenum = nonetexture;
				}
	// 			// naievil -- try to push wad3 loading 
	// 			int index = WAD3_LoadTexture(mt);
	// 			if(index)
	// 			{
	// 				com_netpath[0] = 0;
	// 				tx->gl_texturenum = index;
	// 				tx->fullbright = -1;
	// 				tx->dt_texturenum = 0;

	// //				if(tx_pixels = WAD3_LoadTexture(mt))
	// //				{
	// //					com_netpath[0] = 0;
	// //					tx->gl_texturenum = GL_LoadPalletedTexture (tx_pixels, tx->name, tx->width, tx->height, 0);
	// //					tx->fullbright = -1;
	// //					mapTextureNameList.push_back(tx->gl_texturenum);
	// //					tx->dt_texturenum = 0;

	// 			}
	// 			else
	// 			{
	// 				Con_Printf("Texture %s not found\n", mt->name);		// didn't find the texture in the folder
	// 				com_netpath[0] = 0;
	// 				tx->gl_texturenum = nonetexture;
	// 			}
			
			} else {
				
				int w, h;
				
				unsigned int magic = *((unsigned int*)(data));
				if (magic == 0x65663463)								// what the fuck? 
				{
					w = *((int*)(data + 4));
					h = *((int*)(data + 8));

					tx->gl_texturenum = GL_LoadTexture4(mt->name, w, h, (byte*)(data + 16), GU_LINEAR, qfalse);
				}
	
			}
		}
		else
		{
			sprintf (texname, "textures/%s", mt->name);
			tx->gl_texturenum = loadtextureimage (texname, 0, 0, qfalse, GU_LINEAR);
			if(tx->gl_texturenum == 0)
			{
				tx->gl_texturenum = GL_LoadTexture (mt->name, tx->width, tx->height, (byte *)(tx_pixels), qtrue, GU_LINEAR, level);
			}
/*
		          //Crow_bar mult detail textures
				  sprintf (detname, "gfx/detail/%s", mt->name);
			      tx->dt_texturenum = loadtextureimage (detname, 0, 0, qfalse, GU_LINEAR);
			      mapTextureNameList.push_back(tx->dt_texturenum);
*/
			tx->dt_texturenum = 0;
		          // check for fullbright pixels in the texture - only if it ain't liquid, etc also
			if ((tx->name[0] != '*') && (FindFullbrightTexture ((byte *)(tx_pixels), pixels)))
			{
				// convert any non fullbright pixel to fully transparent
				ConvertPixels ((byte *)(tx_pixels), pixels);

				// get a new name for the fullbright mask to avoid cache mismatches
				sprintf (fbr_mask_name, "fullbright_mask_%s", mt->name);

				// load the fullbright pixels version of the texture
				tx->fullbright =
			        GL_LoadTexture (fbr_mask_name, tx->width, tx->height, (byte *)(tx_pixels), qtrue, GU_LINEAR, level);
			}
			else
				tx->fullbright = -1; // because 0 is a potentially valid texture number
		}
	}
	strcpy(loading_name, mt->name);
        free (tx_pixels);
        loading_cur_step++;
		SCR_UpdateScreen();
	}

//
// sequence the animations
//
	for (i=0 ; i<m->nummiptex ; i++)
	{
		tx = loadmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// allready sequenced

	// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		max = tx->name[1];
		altmax = 0;
		if (max >= 'a' && max <= 'z')
			max -= 'a' - 'A';
		if (max >= '0' && max <= '9')
		{
			max -= '0';
			altmax = 0;
			anims[max] = tx;
			max++;
		}
		else if (max >= 'A' && max <= 'J')
		{
			altmax = max - 'A';
			max = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<m->nummiptex ; j++)
		{
			tx2 = loadmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > max)
					max = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}

#define	ANIM_CYCLE	2
	// link them all together
		for (j=0 ; j<max ; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%max ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (max)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	if (COM_CheckParm ("-lm_1"))
		LIGHTMAP_BYTES = 1;
	else if (COM_CheckParm ("-lm_2"))
		LIGHTMAP_BYTES = 2;
	else if (COM_CheckParm ("-lm_3"))
		LIGHTMAP_BYTES = 3;
	else
        LIGHTMAP_BYTES = 4;

	loadmodel->lightdata = NULL;
	
	if (loadmodel->bspversion == HL_BSPVERSION)
	{
		if (!l->filelen)
		{
			return;
		}
		loadmodel->lightdata = static_cast<byte*>(Hunk_AllocName ( l->filelen, loadname));
		memcpy_vfpu(loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
		return;
	}

	int i;
	byte *in, *out, *data;
	byte d;
	char litfilename[1024];
	// LordHavoc: check for a .lit file
	strcpy(litfilename, loadmodel->name);
	COM_StripExtension(litfilename, litfilename);
	strcat(litfilename, ".lit");
	data = (byte*) COM_LoadHunkFile (litfilename);

	if (data)
	{
      if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I' && data[3] == 'T')
		{
			i = LittleLong(((int *)data)[1]);
			if (i == 1)
			{
				Con_DPrintf("%s loaded", litfilename);
				loadmodel->lightdata = data + 8;
				return;
			}
			else
				Con_Printf("Unknown .lit file version (%d)\n", i);
		}
		else
			Con_Printf("Corrupt .lit file (old version?), ignoring\n");
	}
	// LordHavoc: no .lit found, expand the white lighting data to color

	if (!l->filelen)
	{
  		return;
	}
        loadmodel->lightdata = static_cast<byte*>(Hunk_AllocName ( l->filelen*3, litfilename));
        in = loadmodel->lightdata + l->filelen*2; // place the file at the end, so it will not be overwritten until the very last write
        out = loadmodel->lightdata;
        memcpy_vfpu(in, mod_base + l->fileofs, l->filelen);
        for (i = 0;i < l->filelen;i++)
        {
            d = *in++;
            *out++ = d;
            *out++ = d;
            *out++ = d;
        }
        // LordHavoc: .lit support end

}

// added by dr_mabuse1981

/*
=================
Mod_HL_LoadLighting
=================
*/
void Mod_HL_LoadLighting (lump_t *l)
{
	if (COM_CheckParm ("-lm_1"))
		LIGHTMAP_BYTES = 1;
	else if (COM_CheckParm ("-lm_2"))
		LIGHTMAP_BYTES = 2;
	else if (COM_CheckParm ("-lm_3"))
		LIGHTMAP_BYTES = 3;
	else
    LIGHTMAP_BYTES = 4;

	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}

	loadmodel->lightdata = static_cast<byte*>(Hunk_AllocName ( l->filelen, loadname));
	memcpy_vfpu(loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}

/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
    loadmodel->visdata = NULL;

	if (!l->filelen)
	{
		return;
	}

	loadmodel->visdata = static_cast<byte*>(Hunk_AllocName ( l->filelen, loadname));
	memcpy_vfpu(loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}

/*
=================
Mod_LoadEntities
.ent file support by Crow_bar
=================
*/
void Mod_LoadEntities (lump_t *l)
{
	char entfilename[128];

	loadmodel->entities = NULL;

	strcpy(entfilename, loadmodel->name);
	COM_StripExtension(entfilename, entfilename);
	strcat(entfilename, ".ent");
	ent_file = (byte*) COM_LoadHunkFile (entfilename);

	if (ent_file)
	{
      if (ent_file[0] == '{')
	  {
		Con_DPrintf("%s loaded", entfilename);
		loadmodel->entities = (char*)ent_file;
		return;
	  }
	  else
		Con_Printf("Corrupt .ent file, ignoring\n");
	}

    if (!l->filelen)
	{
		//loadmodel->entities = NULL;
		return;
	}

	loadmodel->entities = static_cast<char*>(Hunk_AllocName ( l->filelen, entfilename));
	memcpy_vfpu(loadmodel->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = reinterpret_cast<dvertex_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<mvertex_t*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = reinterpret_cast<dmodel_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<dmodel_t*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		for (j=0 ; j<MAX_MAP_HULLS ; j++)
			out->headnode[j] = LittleLong (in->headnode[j]);
		out->visleafs = LittleLong (in->visleafs);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = reinterpret_cast<dedge_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<medge_t*>(Hunk_AllocName ( (count + 1) * sizeof(*out), loadname));

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int 	i, j, k, count;
	int		miptex;
	float	len1, len2;

	in = reinterpret_cast<texinfo_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<mtexinfo_t*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<2 ; j++) {
			for (k=0 ; k<4 ; k++) {
				out->vecs[j][k] = LittleFloat (in->vecs[j][k]);
			}
		}
		len1 = Length (out->vecs[0]);
		len2 = Length (out->vecs[1]);
		len1 = (len1 + len2)/2;
		if (len1 < 0.32)
			out->mipadjust = 4;
		else if (len1 < 0.49)
			out->mipadjust = 3;
		else if (len1 < 0.99)
			out->mipadjust = 2;
		else
			out->mipadjust = 1;
#if 0
		if (len1 + len2 < 0.001)
			out->mipadjust = 1;		// don't crash
		else
			out->mipadjust = 1 / floorf( (len1+len2)/2 + 0.1 );
#endif

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);

		if (!loadmodel->textures)
		{
			out->texture = r_notexture_mip;	// checkerboard texture
			out->flags = 0;
		}
		else
		{
			if (miptex >= loadmodel->numtextures)
				Sys_Error ("miptex >= loadmodel->numtextures");
			out->texture = loadmodel->textures[miptex];
			if (!out->texture)
			{
				out->texture = r_notexture_mip; // texture not found
				out->flags = 0;
			}
		}
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;

	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] +
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{
		bmins[i] = (int)floorf(mins[i]/16);
		bmaxs[i] = (int)ceilf(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 512 /* 256 */ )
			Sys_Error ("Bad surface extents");
	}
}


/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;

	in = reinterpret_cast<dface_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<msurface_t*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);
		out->flags = 0;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;

		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + LittleShort (in->texinfo);

		CalcSurfaceExtents (out);

	// lighting info

		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = in->styles[i];
		
		if (loadmodel->bspversion == HL_BSPVERSION)
			i = LittleLong(in->lightofs);
		else
			i = LittleLong(in->lightofs * 3);
		
		if (i == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + (i);

		// cypress -- moved from gu_surface
		// modified to use new TEXFLAG hacky fields and have
		// surfs use the same shabang.
		const char* tex_name = out->texinfo->texture->name;

		// Sky textures.
		if (tex_name[0] == 's' && tex_name[1] == 'k' && tex_name[2] == 'y') {
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);

			GL_Surface(out); // Don't cut up polygon for warps
			continue;
		}
		// Turbulent.
		else if (tex_name[0] == '*') {
			out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);

			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}

			GL_Surface (out);	// Don't cut up polygon for warps
			continue;
		}
		// Don't draw texture and lightmaps.
		else if (tex_name[0] == 'n' && tex_name[1] == 'o' && tex_name[2] == 'd' && tex_name[3] == 'r' && 
		tex_name[4] == 'a' && tex_name[5] == 'w') {
			out->flags |= TEXFLAG_NODRAW;

			continue;
		}
		// Surface uvmaps warp, like metal or glass effects.
		else if ((tex_name[0] == 'e' && tex_name[1] == 'n' && tex_name[2] == 'v') || (tex_name[0] == 'g' && 
		tex_name[1] == 'l' && tex_name[2] == 'a' && tex_name[3] == 's' && tex_name[4] == 's')) {
			out->flags |= TEXFLAG_REFLECT;

			continue;
		} else {
			out->flags |= TEXFLAG_NORMAL;

			continue;
		}
		// cypress -- end modification
	}
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents < 0)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	in = reinterpret_cast<dnode_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<mnode_t*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort (in->firstface);
		out->numsurfaces = LittleShort (in->numfaces);

		for (j=0 ; j<2 ; j++)
		{
			p = LittleShort (in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}
	}

	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;

	in = reinterpret_cast<dleaf_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<mleaf_t*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces +
			LittleShort(in->firstmarksurface);
		out->nummarksurfaces = LittleShort(in->nummarksurfaces);

		p = LittleLong(in->visofs);
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		// gl underwater warp
		if (out->contents != CONTENTS_EMPTY)
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
		}
	}
}

/*
=================
Mod_LoadClipnodes
=================
*/
void Mod_LoadClipnodes (lump_t *l)
{
	dclipnode_t *in, *out;
	int			i, count;
	hull_t		*hull;

	in = reinterpret_cast<dclipnode_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<dclipnode_t*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	if (loadmodel->bspversion == HL_BSPVERSION)
	{
		hull = &loadmodel->hulls[1];
		hull->clipnodes = out;
		hull->firstclipnode = 0;
		hull->lastclipnode = count-1;
		hull->planes = loadmodel->planes;
		hull->clip_mins[0] = -16;
		hull->clip_mins[1] = -16;
		hull->clip_mins[2] = -36;
		hull->clip_maxs[0] = 16;
		hull->clip_maxs[1] = 16;
		hull->clip_maxs[2] = 36;

		hull = &loadmodel->hulls[2];
		hull->clipnodes = out;
		hull->firstclipnode = 0;
		hull->lastclipnode = count-1;
		hull->planes = loadmodel->planes;
		hull->clip_mins[0] = -32;
		hull->clip_mins[1] = -32;
		hull->clip_mins[2] = -32;
		hull->clip_maxs[0] = 32;
		hull->clip_maxs[1] = 32;
		hull->clip_maxs[2] = 32;

	    hull = &loadmodel->hulls[3];
		hull->clipnodes = out;
		hull->firstclipnode = 0;
		hull->lastclipnode = count-1;
		hull->planes = loadmodel->planes;
		hull->clip_mins[0] = -16;
		hull->clip_mins[1] = -16;
		hull->clip_mins[2] = -18;
		hull->clip_maxs[0] = 16;
		hull->clip_maxs[1] = 16;
		hull->clip_maxs[2] = 18;
	}
	else
	{
		hull = &loadmodel->hulls[1];
		hull->clipnodes = out;
		hull->firstclipnode = 0;
		hull->lastclipnode = count-1;
		hull->planes = loadmodel->planes;
		hull->clip_mins[0] = -16;
		hull->clip_mins[1] = -16;
		hull->clip_mins[2] = -24;
		hull->clip_maxs[0] = 16;
		hull->clip_maxs[1] = 16;
		hull->clip_maxs[2] = 32;

		hull = &loadmodel->hulls[2];
		hull->clipnodes = out;
		hull->firstclipnode = 0;
		hull->lastclipnode = count-1;
		hull->planes = loadmodel->planes;
		hull->clip_mins[0] = -32;
		hull->clip_mins[1] = -32;
		hull->clip_mins[2] = -24;
		hull->clip_maxs[0] = 32;
		hull->clip_maxs[1] = 32;
		hull->clip_maxs[2] = 64;
	}

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = LittleLong(in->planenum);
		out->children[0] = LittleShort(in->children[0]);
		out->children[1] = LittleShort(in->children[1]);
	}
}

/*
=================
Mod_MakeHull0

Deplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	dclipnode_t *out;
	int			i, j, count;
	hull_t		*hull;

	hull = &loadmodel->hulls[0];

	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = static_cast<dclipnode_t*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;
		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{
	int		i, j, count;
	short		*in;
	msurface_t **out;

	in = reinterpret_cast<short*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<msurface_t**>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = LittleShort(in[i]);
		if (j >= loadmodel->numsurfaces)
			Sys_Error("bad surface number");
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{
	int		i, count;
	int		*in, *out;

	in = reinterpret_cast<int*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<int*>(Hunk_AllocName ( count*sizeof(*out), loadname));

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;

	in = reinterpret_cast<dplane_t*>(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Con_Printf("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = static_cast<mplane_t*>(Hunk_AllocName ( count*2*sizeof(*out), loadname));

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabsf(mins[i]) > fabsf(maxs[i]) ? fabsf(mins[i]) : fabsf(maxs[i]);
	}

	return Length (corner);
}

/*
=================
Mod_LoadBrushModel
=================
*/

void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int			i, j;
	dheader_t	*header;
    dmodel_t 	*bm;
	loadmodel->type = mod_brush;

	header = (dheader_t *)buffer;

	i = LittleLong (header->version);

	mod->bspversion = LittleLong (header->version);

    if (mod->bspversion != BSPVERSION && mod->bspversion != HL_BSPVERSION)
		Host_Error ("Mod_LoadBrushModel: %s has wrong version number (%i should be %i (Quake) or %i (HalfLife))", mod->name, mod->bspversion, BSPVERSION, HL_BSPVERSION);

    //loadmodel->isworldmodel = !strcmp(loadmodel->name, va("maps/%s.bsp", mapname.string));

	if (mod->bspversion == BSPVERSION && r_hlbsponly.value && !developer.value)
		Host_Error ("Mod_LoadBrushModel: Normal quake maps are disabled. Please use half life bsp. ");


// swap all the lumps
	mod_base = (byte *)header;

	for (i=0 ; i<sizeof(dheader_t)/4 ; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);


    loading_num_step = loading_num_step + 16;
	loading_step = 2;

	strcpy(loading_name, "Vertexes");
	SCR_UpdateScreen ();

	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);

    loading_cur_step++;
	strcpy(loading_name, "Edges");
	SCR_UpdateScreen ();

	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);

    loading_cur_step++;
	strcpy(loading_name, "Surfedges");
	SCR_UpdateScreen ();

	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);

    loading_cur_step++;
	strcpy(loading_name, "Entities");
	SCR_UpdateScreen ();

	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);

    loading_cur_step++;
	strcpy(loading_name, "Textures");
	SCR_UpdateScreen ();

	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);


	if(mod->bspversion == HL_BSPVERSION) // dr_mabuse1981
	{
		Mod_HL_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	}
	else
	{
		Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	}

    loading_cur_step++;
	SCR_UpdateScreen ();

	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);

    loading_cur_step++;
	strcpy(loading_name, "Texinfo");
	SCR_UpdateScreen ();

	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);

    loading_cur_step++;
	strcpy(loading_name, "Faces");
	SCR_UpdateScreen ();

	Mod_LoadFaces (&header->lumps[LUMP_FACES]);

    loading_cur_step++;
	strcpy(loading_name, "Marksurfaces");
	SCR_UpdateScreen ();

	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES]);

    loading_cur_step++;
	strcpy(loading_name, "Visibility");
	SCR_UpdateScreen ();

	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);

    loading_cur_step++;
	strcpy(loading_name, "Leafs");
	SCR_UpdateScreen ();

	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);

    loading_cur_step++;
	strcpy(loading_name, "Nodes");
	SCR_UpdateScreen ();

	Mod_LoadNodes (&header->lumps[LUMP_NODES]);

    loading_cur_step++;
	strcpy(loading_name, "Clipnodes");
	SCR_UpdateScreen ();

	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES]);

    loading_cur_step++;
	strcpy(loading_name, "Submodels");
	SCR_UpdateScreen ();

	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);

    loading_cur_step++;
	strcpy(loading_name, "Hull");
	SCR_UpdateScreen ();

	Mod_MakeHull0 ();
	loading_cur_step++;

	loading_step = 3;

	strcpy(loading_name, "Screen");
    loading_cur_step++;
	SCR_UpdateScreen ();

	mod->numframes = 2;		// regular and alternate animation

//
// set up the submodels (FIXME: this is confusing)
//
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j=1 ; j<MAX_MAP_HULLS ; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes-1;
		}

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);

		mod->numleafs = bm->visleafs;

		if (i < mod->numsubmodels-1)
		{	// duplicate the basic information
			char	name[12];

			snprintf (name, 13, "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
			mod = loadmodel;
		}
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t	*pheader;

stvert_t	stverts[MAXALIASVERTS];
mtriangle_t	triangles[MAXALIASTRIS];

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t	*poseverts[MAXALIASFRAMES];
int			posenum;

byte		**player_8bit_texels_tbl;
byte		*player_8bit_texels;

/*
=================
Mod_LoadAliasFrame
=================
*/
void * Mod_LoadAliasFrame (void * pin, maliasframedesc_t *frame)
{
    //trivertx_t		*pframe;
	trivertx_t		*pinframe;
	int				i;
	//int j;
	daliasframe_t	*pdaliasframe;

	pdaliasframe = (daliasframe_t *)pin;

	strcpy (frame->name, pdaliasframe->name);
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about
		// endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i] - 128;
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i] - 128;
	}


	pinframe = (trivertx_t *)(pdaliasframe + 1);

	poseverts[posenum] = pinframe;

	for (i = 0; i < pheader->numverts; i++) {
		utrivertx_t * unsigned_vert = (utrivertx_t*)&(poseverts[posenum][i]);
		poseverts[posenum][i].v[0] = unsigned_vert->v[0] - 128;
		poseverts[posenum][i].v[1] = unsigned_vert->v[1] - 128;
		poseverts[posenum][i].v[2] = unsigned_vert->v[2] - 128;
	}

	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
void *Mod_LoadAliasGroup (void * pin,  maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	void				*ptemp;

	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i] - 128;
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i] - 128;
	}


	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		poseverts[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);

		for (int j = 0; j < pheader->numverts; j++) {
			utrivertx_t * unsigned_vert = (utrivertx_t*)&(poseverts[posenum][j]);
			poseverts[posenum][j].v[0] = unsigned_vert->v[0] - 128;
			poseverts[posenum][j].v[1] = unsigned_vert->v[1] - 128;
			poseverts[posenum][j].v[2] = unsigned_vert->v[2] - 128;
		}

		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;
/*
extern unsigned d_8to24table[];
*/
// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

void Mod_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte				fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
		{
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
		}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

qboolean model_is_gun(char name[MAX_QPATH])
{
	char wep_path[15];

	for (int i = 0; i < 15; i++) {
		wep_path[i] = name[i];
	}
	wep_path[14] = '\0';

	if (strcmp(wep_path, "models/weapons") == 0) {
		return qtrue;
	}

	return qfalse;
}

qboolean model_is_viewmodel(char * name)
{
	if (strstr(name, "/v_") != NULL) {
		return qtrue;
	}
	return qfalse;
}

qboolean model_is_zombie(char name[MAX_QPATH])
{
	if (strcmp(name, "models/ai/zb%.mdl") == 0 ||
	strcmp(name, "models/ai/zbc%.mdl") == 0 ||
	strcmp(name, "models/ai/zcfull.mdl") == 0 ||
	strcmp(name, "models/ai/zhc^.mdl") == 0 ||
	strcmp(name, "models/ai/zalc(.mdl") == 0 ||
	strcmp(name, "models/ai/zarc(.mdl") == 0 ||
	strcmp(name, "models/ai/zfull.mdl") == 0 ||
	strcmp(name, "models/ai/zh^.mdl") == 0 ||
	strcmp(name, "models/ai/zal(.mdl") == 0 ||
	strcmp(name, "models/ai/zar(.mdl") == 0)
		return qtrue;

	return qfalse;
}

/*
===============
Mod_LoadAllSkins
===============
*/
extern int has_pap;
extern int has_perk_revive;
extern int has_perk_juggernog;
extern int has_perk_speedcola;
extern int has_perk_doubletap;
extern int has_perk_staminup;
extern int has_perk_flopper;
extern int has_perk_deadshot;
extern int has_perk_mulekick;
void *Mod_LoadAllSkins (int numskins, daliasskintype_t *pskintype)
{
	int 	i = 0;
	int		j, k;
	char	name[128], model[64], model2[128];
	int		s;
	//byte	*copy;
	byte	*skin;
	//byte	*texels;
	daliasskingroup_t		*pinskingroup;
	int		groupskins;
	daliasskininterval_t	*pinskinintervals;

	skin = (byte *)(pskintype + 1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Invalid # of skins: %d\n", numskins);

	s = pheader->skinwidth * pheader->skinheight;

	if (model_is_zombie(loadmodel->name) == qtrue) {
		Mod_FloodFillSkin(skin, pheader->skinwidth, pheader->skinheight);
		// force-fill 4 skin slots
		for (int i = 0; i < 4; i++) {
			switch(i) {
				case 0: 
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					break;
				case 1:
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][1];
					break;
				case 2:
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[1][0];
					break;
				case 3:
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[1][1];
					break;
				default: break;
			}
		}

		pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
		return (void *)pskintype;
	} else {
		//
		// General texture override stuff.
		//

		// Mustang & Sally // v_biatch
		if (strcmp(loadmodel->name, "models/weapons/m1911/v_biatch_left.mdl") == 0 ||
		strcmp(loadmodel->name, "models/weapons/m1911/v_biatch_right.mdl") == 0) {
			pheader->gl_texturenum[i][0] = 
			pheader->gl_texturenum[i][1] = 
			pheader->gl_texturenum[i][2] = 
			pheader->gl_texturenum[i][3] = loadtextureimage("models/weapons/m1911/v_biatch.mdl_0", 0, 0, qtrue, GU_LINEAR);

			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
			return (void *)pskintype;
		}

		// Perk Bottles // v_perk
		if (strcmp(loadmodel->name, "models/machines/v_perk.mdl") == 0) {
			for (int i = 0; i < 8; i++) {
				if (i == 0 && has_perk_revive) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = loadpcxas4bpp("models/machines/v_perk.mdl_0", GU_LINEAR);
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				} else if (!has_perk_revive && i == 0) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				}
				if (i == 1 && has_perk_juggernog) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = loadpcxas4bpp("models/machines/v_perk.mdl_1", GU_LINEAR);
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				} else if (!has_perk_juggernog && i == 1) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				}
				if (i == 2 && has_perk_speedcola) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = loadpcxas4bpp("models/machines/v_perk.mdl_2", GU_LINEAR);
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				} else if (!has_perk_speedcola && i == 2) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				}
				if (i == 3 && has_perk_doubletap) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = loadpcxas4bpp("models/machines/v_perk.mdl_3", GU_LINEAR);
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				} else if (!has_perk_doubletap && i == 3) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				}
				if (i == 4 && has_perk_staminup) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = loadpcxas4bpp("models/machines/v_perk.mdl_4", GU_LINEAR);
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				} else if (!has_perk_staminup && i == 4) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				}
				if (i == 5 && has_perk_flopper) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = loadpcxas4bpp("models/machines/v_perk.mdl_5", GU_LINEAR);
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				} else if (!has_perk_flopper && i == 5) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				}
				if (i == 6 && has_perk_deadshot) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = loadpcxas4bpp("models/machines/v_perk.mdl_6", GU_LINEAR);
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				} else if (!has_perk_deadshot && i == 6) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				}
				if (i == 7 && has_perk_mulekick) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = loadpcxas4bpp("models/machines/v_perk.mdl_7", GU_LINEAR);
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				} else if (!has_perk_mulekick && i == 7) {
					pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = zombie_skins[0][0];
					pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
				}
			}

			return (void *)pskintype;
		}
	}

	qboolean is_gun = model_is_gun(loadmodel->name);
	qboolean is_viewmodel = model_is_viewmodel(loadmodel->name);

	for (i=0 ; i<numskins ; i++)
	{
		if (!has_pap && is_gun && i >= 1) {
			pheader->gl_texturenum[i][0] = 
			pheader->gl_texturenum[i][1] = 
			pheader->gl_texturenum[i][2] = 
			pheader->gl_texturenum[i][3] = pheader->gl_texturenum[0][0];
			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
			return (void *)pskintype;
		}
		else if (has_pap && is_gun && i >= 1 && psp_system_model == PSP_MODEL_PHAT) {
			pheader->gl_texturenum[i][0] = 
			pheader->gl_texturenum[i][1] = 
			pheader->gl_texturenum[i][2] = 
			pheader->gl_texturenum[i][3] = loadtextureimage("models/weapons/v_papskin", 0, 0, qtrue, GU_LINEAR);
			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
			return (void *)pskintype;
		}
		else if (pskintype->type == ALIAS_SKIN_SINGLE)
		{
			Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );
			COM_StripExtension(loadmodel->name, model);
			// HACK HACK HACK
			sprintf (model2, "%s.mdl_%i", model, i);
			pheader->gl_texturenum[i][0] = 
			pheader->gl_texturenum[i][1] = 
			pheader->gl_texturenum[i][2] = 
			pheader->gl_texturenum[i][3] = is_viewmodel ? loadtextureimage(model2, 0, 0, qtrue, GU_LINEAR) : loadpcxas4bpp(model2, GU_LINEAR);

			if (pheader->gl_texturenum[i][0] == 0)// did not find a matching TGA...
			{
				sprintf (name, "%s_%i", loadmodel->name, i);

				pheader->gl_texturenum[i][0] =
				pheader->gl_texturenum[i][1] =
				pheader->gl_texturenum[i][2] =
				pheader->gl_texturenum[i][3] = is_viewmodel
					? GL_LoadTexture (name, pheader->skinwidth,pheader->skinheight, (byte *)(pskintype), qtrue, GU_LINEAR, 0)
					: GL_LoadTexture8to4(name, pheader->skinwidth, pheader->skinheight, (byte*)(pskintype+1), (byte*)d_8to24table, GU_LINEAR, 4, NULL);
			}
			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
		}
		else
		{
			// animating skin group.  yuck.
			pskintype++;
			pinskingroup = (daliasskingroup_t *)pskintype;
			groupskins = LittleLong (pinskingroup->numskins);
			pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

			pskintype = reinterpret_cast<daliasskintype_t*>(pinskinintervals + groupskins);

			for (j=0 ; j<groupskins ; j++)
			{
				Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );
				COM_StripExtension(loadmodel->name, model);
				snprintf(model2, 128, "%s_%i_%i", model, i, j);
				pheader->gl_texturenum[i][j&3] = is_viewmodel ? loadtextureimage(model2, 0, 0, qfalse, GU_LINEAR) : loadpcxas4bpp(model2, GU_LINEAR);
				
				if (pheader->gl_texturenum[i][j&3] == 0)// did not find a matching TGA...
				{
					snprintf (name, 128, "%s_%i_%i", loadmodel->name, i, j);
					pheader->gl_texturenum[i][j&3] = is_viewmodel
						? GL_LoadTexture (name, pheader->skinwidth,pheader->skinheight, (byte *)(pskintype), qtrue, GU_LINEAR, 0)
						: GL_LoadTexture8to4(name, pheader->skinwidth, pheader->skinheight, (byte*)(pskintype+1), (byte*)d_8to24table, GU_LINEAR, 4, NULL);
				}
				pskintype = (daliasskintype_t *)((byte *)(pskintype) + s);
			}
			k = j;
			for (/* */; j < 4; j++)
				pheader->gl_texturenum[i][j&3] = pheader->gl_texturenum[i][j - k];
		}
	}

	return (void *)pskintype;
}

//=========================================================================

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i, j;
	mdl_t				*pinmodel;
	stvert_t			*pinstverts;
	dtriangle_t			*pintriangles;
	int					version, numframes; //numskins;
	int					size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int					start, end, total;

	mod->modhint = MOD_NORMAL;

	start = Hunk_LowMark ();

	pinmodel = (mdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size = 	sizeof (aliashdr_t)
			+ (LittleLong (pinmodel->numframes) - 1) *
			sizeof (pheader->frames[0]);
	pheader = static_cast<aliashdr_t*>(Hunk_AllocName (size, loadname));

	mod->flags = LittleLong (pinmodel->flags);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Sys_Error ("model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	pheader->numverts = LittleLong (pinmodel->numverts);

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices", mod->name);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("Invalid # of frames: %d\n", numframes);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = static_cast<synctype_t>(LittleLong (pinmodel->synctype));
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]) + pheader->scale[i] * 128;
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]) + pheader->scale[i] * 128;
	}
//
// load the skins
//
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = static_cast<daliasskintype_t*>(Mod_LoadAllSkins (pheader->numskins, pskintype));

//
// load base s and t vertices
//
	pinstverts = (stvert_t *)pskintype;

	for (i=0 ; i<pheader->numverts ; i++)
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

//
// load triangle lists
//
	pintriangles = (dtriangle_t *)&pinstverts[pheader->numverts];

	for (i=0 ; i<pheader->numtris ; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j=0 ; j<3 ; j++)
		{
			triangles[i].vertindex[j] = LittleLong (pintriangles[i].vertindex[j]);
		}
	}

//
// load the frames
//
	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	//maliasframedesc_t *frame0;

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t	frametype;

		frametype = static_cast<aliasframetype_t>(LittleLong (pframetype->type));

		/*if(i == 0)//see this right here? blubs can type, but something is a-miss with the precalculated mdl min and max values, so let's not use this.
		{
			frame0 = &pheader->frames[0];

			mod->mins[0] = frame0->bboxmin.v[0];
			mod->mins[1] = frame0->bboxmin.v[1];
			mod->mins[2] = frame0->bboxmin.v[2];
			mod->maxs[0] = frame0->bboxmax.v[0];
			mod->maxs[1] = frame0->bboxmax.v[1];
			mod->maxs[2] = frame0->bboxmax.v[2];
		}*/
		if (frametype == ALIAS_SINGLE)
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		}
		else
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
		}
	}


	pheader->numposes = posenum;

	mod->type = mod_alias;

// FIXME: do this right
	mod->mins[0] = mod->mins[1] = mod->mins[2] = -32;
	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 32;

	//
	// build the draw lists
	//
	GL_MakeAliasModelDisplayLists (mod, pheader);

//
// move the complete, relocatable alias model to the cache
//

	end = Hunk_LowMark ();
	total = end - start;

	// cypress -- in rare instances the viewmodel is able to fuck this
	// up and try to allocate.. again.. let's tell it no and add this bound
	if (!mod->cache.data)
		Cache_Alloc (&mod->cache, total, loadname);

	if (!mod->cache.data)
		return;

	memcpy_vfpu(mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}


int loadtextureimage (char* filename, int matchwidth, int matchheight, qboolean complain, int filter);

extern char	loadname[32];	// for hunk tags

/*
=================
Mod_LoadSpriteFrame
=================
*/
void * Mod_LoadSpriteFrame (void * pin, mspriteframe_t **ppframe, int framenum, int version, byte *palette)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[128], sprite[64], sprite2[128];

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = static_cast<mspriteframe_t*>(Hunk_AllocName (sizeof (mspriteframe_t),loadname));

	Q_memset (pspriteframe, 0, sizeof (mspriteframe_t));

	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	// HACK HACK HACK
	snprintf(name, 128, "%s.spr_%i", loadmodel->name, framenum);

	if (version == SPRITE_VERSION)
	{
		COM_StripExtension(loadmodel->name, sprite);
		snprintf (sprite2, 128, "%s.spr_%i", sprite, framenum);
		pspriteframe->gl_texturenum = loadtextureimage (sprite2, 0, 0, qtrue, GU_LINEAR);
		
		if (pspriteframe->gl_texturenum == 0)// did not find a matching TGA...
		{
			pspriteframe->gl_texturenum = GL_LoadTexture (name, width, height, (byte *)(pinframe + 1), qtrue, GU_LINEAR, 0);
		}
	}
	else if (version == SPRITE32_VERSION)
	{
		size *= 4;
		pspriteframe->gl_texturenum = GL_LoadImages (name, width, height, (byte *)(pinframe + 1), qtrue, GU_LINEAR, 0, 4);
	}
	else
	{
		Sys_Error("Non sprite type");
	}

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void * Mod_LoadSpriteGroup (void * pin, mspriteframe_t **ppframe, int framenum, int version, byte *palette)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *)pin;


	numframes = LittleLong (pingroup->numframes);

	pspritegroup = static_cast<mspritegroup_t*>(Hunk_AllocName (sizeof (mspritegroup_t) +
				(numframes - 1) * sizeof (pspritegroup->frames[0]), loadname));


	pspritegroup->numframes = numframes;

   	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = static_cast<float*>(Hunk_AllocName (numframes * sizeof (float), loadname));

	pspritegroup->intervals = poutintervals;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
		{
			Sys_Error ("interval<=0");
		}
		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i], framenum * 100 + i, version, palette);
	}

	return ptemp;
}

/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	int					i;
	int					version;
	//dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;
    byte palette[256*4];
	int sptype;

	version = LittleLong (((dsprite_t *)buffer)->version);

	switch (version)
	{
	  case SPRITE_VERSION:
      case SPRITE32_VERSION:
		break;
	  default:
       Sys_Error ("%s has wrong version number (%i should be %i(q1), %i(dp))", mod->name, version, SPRITE_VERSION, SPRITE32_VERSION);
	}

    dsprite_t *pinqsprite;
	pinqsprite = (dsprite_t *)buffer;

    sptype = LittleLong (pinqsprite->type);

	numframes = LittleLong (pinqsprite->numframes);

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (psprite->frames);

	psprite = static_cast<msprite_t*>(Hunk_AllocName (size, loadname));

	mod->cache.data = psprite;
	psprite->type = sptype;

	mod->synctype = static_cast<synctype_t>(LittleLong (pinqsprite->synctype));

	psprite->numframes = numframes;
    psprite->maxwidth = LittleLong (pinqsprite->width);
	psprite->maxheight = LittleLong (pinqsprite->height);
	psprite->beamlength = LittleFloat (pinqsprite->beamlength);

	pframetype = (dspriteframetype_t *)(pinqsprite + 1);

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;

//
// load the frames
//
	if (numframes < 1)
	{
		Con_Printf ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);
	}

	mod->numframes = numframes;

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t	frametype;

		frametype = static_cast<spriteframetype_t>(LittleFloat(pframetype->type));
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteFrame (pframetype + 1,
										 &psprite->frames[i].frameptr, i, version, palette);
		}
		else
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteGroup (pframetype + 1,
										 &psprite->frames[i].frameptr, i, version, palette);
		}
	}

	mod->type = mod_sprite;
}

/*
=================
Mod_LoadQ2SpriteModel
By Crow_bar
=================
*/
qboolean Mod_LoadQ2SpriteModel (model_t *mod, void *buffer)
{
	int					i;
	int					version;
	dmd2sprite_t		*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dmd2sprframe_t		*pframetype;
	mspriteframe_t		*frame;
	float origin[2];
	int hunkstart;

	hunkstart = Hunk_LowMark();

	pin = (dmd2sprite_t *)buffer;

	version = LittleLong (pin->version);
	if (version != SPRITE2_VERSION)
	{
		Sys_Error ("%s has wrong version number "
				 "(%i should be %i)", mod->name, version, SPRITE2_VERSION);
		return qfalse;
	}

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (psprite->frames);

	psprite = static_cast<msprite_t*>(Hunk_AllocName (size, loadname));

	mod->cache.data = psprite;

	psprite->type = SPR_VP_PARALLEL;
	psprite->maxwidth = 1;
	psprite->maxheight = 1;
	psprite->beamlength = 1;
	mod->synctype = static_cast<synctype_t>(1);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;

//
// load the frames
//
	if (numframes < 1)
	{
		Sys_Error ("Invalid # of frames: %d\n", numframes);
		Hunk_FreeToLowMark(hunkstart);
		return qfalse;
	}

	mod->numframes = numframes;

	pframetype = pin->frames;

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t	frametype;

		frametype = SPR_SINGLE;
		psprite->frames[i].type = frametype;

		frame = psprite->frames[i].frameptr = static_cast<mspriteframe_t*>(Hunk_AllocName(sizeof(mspriteframe_t), loadname));

		frame->gl_texturenum = loadtextureimage (pframetype->name, 0, 0, qtrue, GU_LINEAR);

		frame->width = LittleLong(pframetype->width);
		frame->height = LittleLong(pframetype->height);
		origin[0] = LittleLong (pframetype->origin_x);
		origin[1] = LittleLong (pframetype->origin_y);
#if 0 //quake 2 negative translate
		frame->up = -origin[1];
		frame->down = frame->height - origin[1];
		frame->left = -origin[0];
		frame->right = frame->width - origin[0];
#else
        frame->up    = origin[1];
        frame->down  = origin[1] - frame->height;
		frame->left  = -origin[0];
		frame->right = -origin[0] + frame->width;
#endif

		pframetype++;
	}

	mod->type = mod_sprite;

	return qtrue;
}

//=============================================================================

/*
================
Mod_Print
================
*/
extern "C" void Mod_Print (void)
{
	int		i;
	model_t	*mod;

	Con_Printf ("Cached models:\n");
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		Con_Printf ("%8p : %s\n",mod->cache.data, mod->name);
	}
}


