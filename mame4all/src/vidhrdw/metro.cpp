/***************************************************************************

							  -= Metro Games =-

				driver by	Luca Elia (l.elia@tin.it)


							[ 3 Scrolling Layers ]

		There is memory for a huge layer, but the actual tilemap
		is a smaller window (of fixed size) carved from anywhere
		inside that layer.

		Tile Size:				  	8 x 8 x 4
		(later games can switch to  8 x 8 x 8, 16 x 16 x 4/8 at run time)

		Big Layer Size:			2048 x 2048 (8x8 tiles) or 4096 x 4096 (16x16 tiles)

		Tilemap Window Size:	512 x 256 (8x8 tiles) or 1024 x 512 (16x16 tiles)

		The tile codes in memory do not map directly to tiles. They
		are indexes into a table (with 0x200 entries) that defines
		a virtual set of tiles for the 3 layers. Each entry in that
		table adds 16 tiles to the set of available tiles, and decides
		their color code.

		Tile code with their msbit set are different as they mean:
		draw a tile filled with a single color (0-1ff)


							[ 512 Zooming Sprites ]

		The sprites are NOT tile based: the "tile" size can vary from
		8 to 64 (independently for width and height) with an 8 pixel
		granularity. The "tile" address is a multiple of 8x8 pixels.

		Each sprite can be shrinked to ~1/4 or enlarged to ~32x following
		an exponential curve of sizes (with one zoom value for both width
		and height)


***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"

/* Variables that driver has access to: */

data16_t *metro_videoregs;
data16_t *metro_screenctrl;
data16_t *metro_scroll;
data16_t *metro_tiletable;
size_t metro_tiletable_size;
data16_t *metro_vram_0,*metro_vram_1,*metro_vram_2;
data16_t *metro_window;

static int support_8bpp,support_16x16;
static int has_zoom;


data16_t *metro_K053936_ram,*metro_K053936_ctrl;
static struct tilemap *metro_K053936_tilemap;

static data16_t *metro_tiletable_old;


static void metro_K053936_get_tile_info(int tile_index)
{
  int code = metro_K053936_ram[tile_index];

  SET_TILE_INFO(
    2,
    code & 0x7fff,
    0x1e,
    0)
}


WRITE16_HANDLER( metro_K053936_w )
{
  data16_t oldword = metro_K053936_ram[offset];
  COMBINE_DATA(&metro_K053936_ram[offset]);
  if (oldword != metro_K053936_ram[offset])
    tilemap_mark_tile_dirty(metro_K053936_tilemap,offset);
}

void K053936_zoom_draw(struct osd_bitmap *bitmap)
{
  UINT32 startx,starty;
  int incxx,incxy,incyx,incyy;
  struct osd_bitmap *srcbitmap = tilemap_get_pixmap(metro_K053936_tilemap);

  startx = 256 * (INT16)(metro_K053936_ctrl[0x00]);
  incxx  =       (INT16)(metro_K053936_ctrl[0x03]);
  incyx  =       (INT16)(metro_K053936_ctrl[0x02]);
  starty = 256 * (INT16)(metro_K053936_ctrl[0x01]);
  incxy  =       (INT16)(metro_K053936_ctrl[0x05]);
  incyy  =       (INT16)(metro_K053936_ctrl[0x04]);

  startx += 21 * incyx;
  starty += 21 * incyy;

  startx += 69 * incxx;
  starty += 69 * incxy;

  copyrozbitmap(bitmap,srcbitmap,startx << 5,starty << 5,
                incxx << 5,incxy << 5,incyx << 5,incyy << 5,0,
                &Machine->visible_area,TRANSPARENCY_NONE,0,0);
#if 0
  usrintf_showmessage("%04x%04x%04x%04x %04x%04x%04x%04x\n%04x%04x%04x%04x %04x%04x%04x%04x",
                      metro_K053936_ctrl[0x00],
                      metro_K053936_ctrl[0x01],
                      metro_K053936_ctrl[0x02],
                      metro_K053936_ctrl[0x03],
                      metro_K053936_ctrl[0x04],
                      metro_K053936_ctrl[0x05],
                      metro_K053936_ctrl[0x06],
                      metro_K053936_ctrl[0x07],
                      metro_K053936_ctrl[0x08],
                      metro_K053936_ctrl[0x09],
                      metro_K053936_ctrl[0x0a],
                      metro_K053936_ctrl[0x0b],
                      metro_K053936_ctrl[0x0c],
                      metro_K053936_ctrl[0x0d],
                      metro_K053936_ctrl[0x0e],
                      metro_K053936_ctrl[0x0f]);
#endif
}



/***************************************************************************


							Palette GGGGGRRRRRBBBBBx


***************************************************************************/

WRITE16_HANDLER( metro_paletteram_w )
{
  int r,g,b;

  data = COMBINE_DATA(&paletteram16[offset]);

  b = (data >>  1) & 0x1f;
  r = (data >>  6) & 0x1f;
  g = (data >> 11) & 0x1f;

  /* We need the ^0xff because we had to invert the pens in the gfx */
  palette_change_color(offset^0xff,(r << 3) | (r >> 2),(g << 3) | (g >> 2),(b << 3) | (b >> 2));
}


/***************************************************************************


						Tilemaps: Tiles Set & Window

	Each entry in the Tiles Set RAM uses 2 words to specify a starting
	tile code and a color code. This adds 16 consecutive tiles with
	that color code to the set of available tiles.

		Offset:		Bits:					Value:

		0.w			fedc ---- ---- ----
					---- ba98 7654 ----		Color Code
					---- ---- ---- 3210		Code High Bits

		2.w									Code Low Bits


***************************************************************************/


/***************************************************************************


							Tilemaps: Rendering


***************************************************************************/

static struct tilemap *tilemap[3];
static struct tilemap *tilemap_16x16[3];
static UINT8 *empty_tiles;

/* A 2048 x 2048 virtual tilemap */

#define BIG_NX		(0x100)
#define BIG_NY		(0x100)

/* A smaller 512 x 256 window defines the actual tilemap */

#define WIN_NX		(0x40)
#define WIN_NY		(0x20)
//#define WIN_NX		(0x40+1)
//#define WIN_NY		(0x20+1)


/* 8x8x4 tiles only */
INLINE void get_tile_info(int tile_index,int layer,data16_t *vram)
{
  data16_t code;
  int      table_index;
  UINT32   tile;

  /* The actual tile index depends on the window */
  tile_index	=	((tile_index / WIN_NX + metro_window[layer * 2 + 0] / 8) % BIG_NY) * BIG_NX +
                ((tile_index % WIN_NX + metro_window[layer * 2 + 1] / 8) % BIG_NX);

  /* Fetch the code */
  code			=	vram[ tile_index ];

  /* Use it as an index into the tiles set table */
  table_index		=	( (code & 0x1ff0) >> 4 ) * 2;
  tile			=	(metro_tiletable[table_index + 0] << 16 ) +
              metro_tiletable[table_index + 1];

  if (code & 0x8000) /* Special: draw a tile of a single color (i.e. not from the gfx ROMs) */
  {
    int _code = code & 0x000f;
    tile_info.tile_number = _code;
    tile_info.pen_data = empty_tiles + _code*16*16;
    tile_info.pal_data = &Machine->remapped_colortable[(((code & 0x0ff0) ^ 0x0f0) + 0x1000)];
    tile_info.pen_usage = 0;
    tile_info.flags = 0;
  }
  else
    SET_TILE_INFO(
      0,
      (tile & 0xfffff) + (code & 0xf),
      (((tile & 0x0ff00000) >> 20) ^ 0x0f) + 0x100,
      TILE_FLIPXY((code & 0x6000) >> 13))
  }


/* 8x8x4 or 8x8x8 tiles. It's the tile's color that decides: if its low 4
   bits are high ($f,$1f,$2f etc) the tile is 8bpp, otherwise it's 4bpp */
INLINE void get_tile_info_8bit(int tile_index,int layer,data16_t *vram)
{
  data16_t code;
  int      table_index;
  UINT32   tile;

  tile_index	=	((tile_index / WIN_NX + metro_window[layer * 2 + 0] / 8) % BIG_NY) * BIG_NX +
                ((tile_index % WIN_NX + metro_window[layer * 2 + 1] / 8) % BIG_NX);

  code			=	vram[ tile_index ];
  table_index		=	( (code & 0x1ff0) >> 4 ) * 2;
  tile			=	(metro_tiletable[table_index + 0] << 16 ) +
              metro_tiletable[table_index + 1];

  if (code & 0x8000) /* Special: draw a tile of a single color (i.e. not from the gfx ROMs) */
  {
    int _code = code & 0x000f;
    tile_info.tile_number = _code;
    tile_info.pen_data = empty_tiles + _code*16*16;
    tile_info.pal_data = &Machine->remapped_colortable[(((code & 0x0ff0) ^ 0x0f0) + 0x1000)];
    tile_info.pen_usage = 0;
    tile_info.flags = 0;
  }
  else if ((tile & 0x00f00000)==0x00f00000)	/* draw tile as 8bpp */
    SET_TILE_INFO(
      1,
      (tile & 0xfffff) + 2*(code & 0xf),
      ((tile & 0x0f000000) >> 24) + 0x10,
      TILE_FLIPXY((code & 0x6000) >> 13))
    else
      SET_TILE_INFO(
        0,
        (tile & 0xfffff) + (code & 0xf),
        (((tile & 0x0ff00000) >> 20) ^ 0x0f) + 0x100,
        TILE_FLIPXY((code & 0x6000) >> 13))
    }

/* 16x16x4 or 16x16x8 tiles. It's the tile's color that decides: if its low 4
   bits are high ($f,$1f,$2f etc) the tile is 8bpp, otherwise it's 4bpp */
INLINE void get_tile_info_16x16_8bit(int tile_index,int layer,data16_t *vram)
{
  data16_t code;
  int      table_index;
  UINT32   tile;

  /* The actual tile index depends on the window */
  tile_index	=	((tile_index / WIN_NX + metro_window[layer * 2 + 0] / 8) % BIG_NY) * BIG_NX +
                ((tile_index % WIN_NX + metro_window[layer * 2 + 1] / 8) % BIG_NX);

  /* Fetch the code */
  code			=	vram[ tile_index ];

  /* Use it as an index into the tiles set table */
  table_index		=	( (code & 0x1ff0) >> 4 ) * 2;
  tile			=	(metro_tiletable[table_index + 0] << 16 ) +
              metro_tiletable[table_index + 1];

  if (code & 0x8000) /* Special: draw a tile of a single color (i.e. not from the gfx ROMs) */
  {
    int _code = code & 0x000f;
    tile_info.tile_number = _code;
    tile_info.pen_data = empty_tiles + _code*16*16;
    tile_info.pal_data = &Machine->remapped_colortable[(((code & 0x0ff0) ^ 0x0f0) + 0x1000)];
    tile_info.pen_usage = 0;
    tile_info.flags = 0;
  }
  else if ((tile & 0x00f00000)==0x00f00000)	/* draw tile as 8bpp */
    SET_TILE_INFO(
      3,
      (tile & 0xfffff) + 8*(code & 0xf),
      ((tile & 0x0f000000) >> 24) + 0x10,
      TILE_FLIPXY((code & 0x6000) >> 13))
    else
      SET_TILE_INFO(
        2,
        (tile & 0xfffff) + 4*(code & 0xf),
        (((tile & 0x0ff00000) >> 20) ^ 0x0f) + 0x100,
        TILE_FLIPXY((code & 0x6000) >> 13))
    }


INLINE void metro_vram_w(offs_t offset,data16_t data,data16_t mem_mask,int layer,data16_t *vram)
{
  data16_t olddata = vram[offset];
  data16_t newdata = COMBINE_DATA(&vram[offset]);
  if ( newdata != olddata )
  {
    /* Account for the window */
    int col		=	(offset % BIG_NX) - ((metro_window[layer * 2 + 1] / 8) % BIG_NX);
    int row		=	(offset / BIG_NX) - ((metro_window[layer * 2 + 0] / 8) % BIG_NY);
    if (col < -(BIG_NX-WIN_NX))	col += (BIG_NX-WIN_NX) + WIN_NX;
    if (row < -(BIG_NY-WIN_NY))	row += (BIG_NY-WIN_NY) + WIN_NY;
    if	( (col >= 0) && (col < WIN_NX) &&
          (row >= 0) && (row < WIN_NY) )
    {
      tilemap_mark_tile_dirty(tilemap[layer], row * WIN_NX + col );
      if (tilemap_16x16[layer])
        tilemap_mark_tile_dirty(tilemap_16x16[layer], row * WIN_NX + col );
    }
  }
}



static void get_tile_info_0(int tile_index)
{
  get_tile_info(tile_index,0,metro_vram_0);
}
static void get_tile_info_1(int tile_index)
{
  get_tile_info(tile_index,1,metro_vram_1);
}
static void get_tile_info_2(int tile_index)
{
  get_tile_info(tile_index,2,metro_vram_2);
}

static void get_tile_info_0_8bit(int tile_index)
{
  get_tile_info_8bit(tile_index,0,metro_vram_0);
}
static void get_tile_info_1_8bit(int tile_index)
{
  get_tile_info_8bit(tile_index,1,metro_vram_1);
}
static void get_tile_info_2_8bit(int tile_index)
{
  get_tile_info_8bit(tile_index,2,metro_vram_2);
}

static void get_tile_info_0_16x16_8bit(int tile_index)
{
  get_tile_info_16x16_8bit(tile_index,0,metro_vram_0);
}
static void get_tile_info_1_16x16_8bit(int tile_index)
{
  get_tile_info_16x16_8bit(tile_index,1,metro_vram_1);
}
static void get_tile_info_2_16x16_8bit(int tile_index)
{
  get_tile_info_16x16_8bit(tile_index,2,metro_vram_2);
}

WRITE16_HANDLER( metro_vram_0_w )
{
  metro_vram_w(offset,data,mem_mask,0,metro_vram_0);
}
WRITE16_HANDLER( metro_vram_1_w )
{
  metro_vram_w(offset,data,mem_mask,1,metro_vram_1);
}
WRITE16_HANDLER( metro_vram_2_w )
{
  metro_vram_w(offset,data,mem_mask,2,metro_vram_2);
}



/* Dirty the relevant tilemap when its window changes */
WRITE16_HANDLER( metro_window_w )
{
  data16_t olddata = metro_window[offset];
  data16_t newdata = COMBINE_DATA( &metro_window[offset] );
  if ( newdata != olddata )
  {
    offset /= 2;
    tilemap_mark_all_tiles_dirty(tilemap[offset]);
    if (tilemap_16x16[offset]) tilemap_mark_all_tiles_dirty(tilemap_16x16[offset]);
  }
}



/***************************************************************************


							Video Init Routines


***************************************************************************/

/*
 Sprites are not tile based, so we decode their graphics at runtime.

 We can't do it at startup because drawgfx requires the tiles to be
 pre-rotated to support vertical games, and that, in turn, requires
 the tile's sizes to be known at startup - which we don't!
*/

int metro_sprite_xoffs, metro_sprite_yoffs;


void metro_vh_stop(void)
{
  free(empty_tiles);
  empty_tiles = NULL;

  free(metro_tiletable_old);
  metro_tiletable_old = NULL;
}

static void alloc_empty_tiles(void)
{
  int code,i;

  empty_tiles = malloc(16*16*16);
  if (!empty_tiles) return;

  for (code = 0; code < 0x10; code++)
    for (i = 0; i < 16*16; i++)
      empty_tiles[16*16*code + i] = code ^ 0x0f;
}

int metro_vh_start_14100(void)
{
  support_8bpp = 0;
  support_16x16 = 0;
  has_zoom = 0;

  alloc_empty_tiles();
  metro_tiletable_old = malloc(metro_tiletable_size);

  tilemap[0] = tilemap_create(get_tile_info_0,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);
  tilemap[1] = tilemap_create(get_tile_info_1,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);
  tilemap[2] = tilemap_create(get_tile_info_2,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);

  tilemap_16x16[0] = NULL;
  tilemap_16x16[1] = NULL;
  tilemap_16x16[2] = NULL;

  if (!tilemap[0] || !tilemap[1] || !tilemap[2] || !empty_tiles || !metro_tiletable_old)
  {
    metro_vh_stop();
    return 1;
  }

  tilemap_set_transparent_pen(tilemap[0],0);
  tilemap_set_transparent_pen(tilemap[1],0);
  tilemap_set_transparent_pen(tilemap[2],0);

  return 0;
}

int metro_vh_start_14220(void)
{
  support_8bpp = 1;
  support_16x16 = 0;
  has_zoom = 0;

  alloc_empty_tiles();
  metro_tiletable_old = malloc(metro_tiletable_size);

  tilemap[0] = tilemap_create(get_tile_info_0_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);
  tilemap[1] = tilemap_create(get_tile_info_1_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);
  tilemap[2] = tilemap_create(get_tile_info_2_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);

  tilemap_16x16[0] = NULL;
  tilemap_16x16[1] = NULL;
  tilemap_16x16[2] = NULL;

  if (!tilemap[0] || !tilemap[1] || !tilemap[2] || !empty_tiles || !metro_tiletable_old)
  {
    metro_vh_stop();
    return 1;
  }

  tilemap_set_transparent_pen(tilemap[0],0);
  tilemap_set_transparent_pen(tilemap[1],0);
  tilemap_set_transparent_pen(tilemap[2],0);

  tilemap_set_scrolldx(tilemap[0], -2, 2);
  tilemap_set_scrolldx(tilemap[1], -2, 2);
  tilemap_set_scrolldx(tilemap[2], -2, 2);

  return 0;
}

int metro_vh_start_14300(void)
{
  support_8bpp = 1;
  support_16x16 = 1;
  has_zoom = 0;

  alloc_empty_tiles();
  metro_tiletable_old = malloc(metro_tiletable_size);

  tilemap[0] = tilemap_create(get_tile_info_0_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);
  tilemap[1] = tilemap_create(get_tile_info_1_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);
  tilemap[2] = tilemap_create(get_tile_info_2_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,8,8,WIN_NX,WIN_NY);

  tilemap_16x16[0] = tilemap_create(get_tile_info_0_16x16_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,16,16,WIN_NX,WIN_NY);
  tilemap_16x16[1] = tilemap_create(get_tile_info_1_16x16_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,16,16,WIN_NX,WIN_NY);
  tilemap_16x16[2] = tilemap_create(get_tile_info_2_16x16_8bit,tilemap_scan_rows,TILEMAP_TRANSPARENT,16,16,WIN_NX,WIN_NY);

  if (!tilemap[0] || !tilemap[1] || !tilemap[2]
      || !tilemap_16x16[0] || !tilemap_16x16[1] || !tilemap_16x16[2]
      || !empty_tiles || !metro_tiletable_old)
  {
    metro_vh_stop();
    return 1;
  }

  tilemap_set_transparent_pen(tilemap[0],0);
  tilemap_set_transparent_pen(tilemap[1],0);
  tilemap_set_transparent_pen(tilemap[2],0);
  tilemap_set_transparent_pen(tilemap_16x16[0],0);
  tilemap_set_transparent_pen(tilemap_16x16[1],0);
  tilemap_set_transparent_pen(tilemap_16x16[2],0);

  return 0;
}

int  blzntrnd_vh_start(void)
{
  if (metro_vh_start_14220())
    return 1;

  has_zoom = 1;

  metro_K053936_tilemap = tilemap_create(metro_K053936_get_tile_info, tilemap_scan_rows,
                                         TILEMAP_OPAQUE, 8,8, 256, 512 );

  if (!metro_K053936_tilemap)
    return 1;

  tilemap_set_scrolldx(tilemap[0], 8, -8);
  tilemap_set_scrolldx(tilemap[1], 8, -8);
  tilemap_set_scrolldx(tilemap[2], 8, -8);

  return 0;
}


/***************************************************************************

								Video Registers


			Offset:				Value:

			0.w					Number Of Sprites To Draw
			2.w					? $8100, $913 (bit 8 = priority)
			4.w					Sprites Y Offset
			6.w					Sprites X Offset
			8.w					Sprites Color Codes Start

			-

			10.w				Layers order (usually $24 -> 2,1,0)
								76-- ----
								--54 ---- Background
								---- 32-- Middleground
								---- --10 Foreground

			12.w				? $fff,$ffe,$e0f


***************************************************************************/



/***************************************************************************


								Sprites Drawing


		Offset:		Bits:					Value:

		0.w			fedc b--- ---- ----		Priority (0 = Max)
					---- -a98 7654 3210		X

		2.w			fedc ba-- ---- ----		Zoom (Both X & Y)
					---- --98 7654 3210		Y

		4.w			f--- ---- ---- ----		Flip X
					-e-- ---- ---- ----		Flip Y
					--dc b--- ---- ----		Size X *
					---- -a98 ---- ----		Size Y *
					---- ---- 7654 ----		Color
					---- ---- ---- 3210		Code High Bits **

		6.w									Code Low Bits  **

*  8 pixel increments
** 8x8 pixel increments

***************************************************************************/

/* Draw sprites of a given priority */

void metro_draw_sprites(struct osd_bitmap *bitmap, int pri)
{
  const int region		=	REGION_GFX1;

  unsigned char *base_gfx	=	memory_region(region);
  unsigned char *gfx_max	=	base_gfx + memory_region_length(region);

  int max_x				=	Machine->drv->screen_width;
  int max_y				=	Machine->drv->screen_height;

  int max_sprites			=	spriteram_size / 8;
  int sprites				=	metro_videoregs[0x00/2] % max_sprites;

  data16_t *src			=	spriteram16 + (sprites - 1) * (8/2);
  data16_t *end			=	spriteram16;

  int color_start			=	((metro_videoregs[0x08/2] & 0xf) << 4 ) + 0x100;

  pri = (~pri & 0x1f) << (16-5);

  for ( ; src >= end; src -= 8/2 )
  {
    int x,y, attr,code,color,flipx,flipy, zoom, curr_pri,width,height;
    unsigned char *gfxdata;

    /* Exponential zoom table extracted from daitoride */
    const int zoomtable[0x40] =
    {
      0xAAC,0x800,0x668,0x554,0x494,0x400,0x390,0x334,
      0x2E8,0x2AC,0x278,0x248,0x224,0x200,0x1E0,0x1C8,
      0x1B0,0x198,0x188,0x174,0x164,0x154,0x148,0x13C,
      0x130,0x124,0x11C,0x110,0x108,0x100,0x0F8,0x0F0,
      0x0EC,0x0E4,0x0DC,0x0D8,0x0D4,0x0CC,0x0C8,0x0C4,
      0x0C0,0x0BC,0x0B8,0x0B4,0x0B0,0x0AC,0x0A8,0x0A4,
      0x0A0,0x09C,0x098,0x094,0x090,0x08C,0x088,0x080,
      0x078,0x070,0x068,0x060,0x058,0x050,0x048,0x040
    };

    x					=	src[ 0 ];
    curr_pri			=	x & 0xf800;
    if ( (curr_pri == 0xf800) || (curr_pri != pri) )	continue;
    y					=	src[ 1 ];
    attr				=	src[ 2 ];
    code				=	src[ 3 ];

    flipx				=	attr & 0x8000;
    flipy				=	attr & 0x4000;
    color				=   (attr & 0xf0) >> 4;

    zoom				=	zoomtable[(y & 0xfc00) >> 10] << (16-8);

    x					=	(x & 0x07ff) - metro_sprite_xoffs;
    y					=	(y & 0x03ff) - metro_sprite_yoffs;

    width				= (( (attr >> 11) & 0x7 ) + 1 ) * 8;
    height				= (( (attr >>  8) & 0x7 ) + 1 ) * 8;

    gfxdata		=	base_gfx + (8*8*4/8) * (((attr & 0x000f) << 16) + code);

    if (flip_screen)
    {
      flipx = !flipx;
      x = max_x - x - width;
      flipy = !flipy;
      y = max_y - y - height;
    }

    if (support_8bpp && color == 0xf)	/* 8bpp */
    {
      /* prepare GfxElement on the fly */
      struct GfxElement gfx;
      gfx.width = width;
      gfx.height = height;
      gfx.total_elements = 1;
      gfx.color_granularity = 256;
      gfx.colortable = Machine->remapped_colortable;
      gfx.total_colors = 0x20;
      gfx.pen_usage = NULL;
      gfx.gfxdata = gfxdata;
      gfx.line_modulo = width;
      gfx.char_modulo = 0;	/* doesn't matter */
      gfx.flags = 0;
      if (Machine->orientation & ORIENTATION_SWAP_XY)
        gfx.flags |= GFX_SWAPXY;

      /* Bounds checking */
      if ( (gfxdata + width * height - 1) >= gfx_max )
        continue;

      drawgfxzoom(	bitmap,&gfx,
                    0,
                    color_start >> 4,
                    flipx, flipy,
                    x, y,
                    &Machine->visible_area, TRANSPARENCY_PEN, 0,
                    zoom, zoom	);
    }
    else
    {
      /* prepare GfxElement on the fly */
      struct GfxElement gfx;
      gfx.width = width;
      gfx.height = height;
      gfx.total_elements = 1;
      gfx.color_granularity = 16;
      gfx.colortable = Machine->remapped_colortable;
      gfx.total_colors = 0x200;
      gfx.pen_usage = NULL;
      gfx.gfxdata = gfxdata;
      gfx.line_modulo = width/2;
      gfx.char_modulo = 0;	/* doesn't matter */
      gfx.flags = GFX_PACKED;
      if (Machine->orientation & ORIENTATION_SWAP_XY)
      {
        gfx.flags |= GFX_SWAPXY;
        gfx.width = height;
        gfx.height = width;
      }

      /* Bounds checking */
      if ( (gfxdata + width/2 * height - 1) >= gfx_max )
        continue;

      drawgfxzoom(	bitmap,&gfx,
                    0,
                    (color ^ 0x0f) + color_start,
                    flipx, flipy,
                    x, y,
                    &Machine->visible_area, TRANSPARENCY_PEN, 0,
                    zoom, zoom	);
    }

#if 0
    {
      /* Display priority + zoom on each sprite */
      struct DisplayText dt[2];
      char buf[80];
      sprintf(buf, "%02X %02X",((src[ 0 ] & 0xf800) >> 11)^0x1f,((src[ 1 ] & 0xfc00) >> 10) );
      dt[0].text = buf;
      dt[0].color = UI_COLOR_NORMAL;
      dt[0].x = x;
      dt[0].y = y;
      dt[1].text = 0; /* terminate array */
      displaytext(Machine->scrbitmap,dt);
    }
#endif
  }
}


void metro_mark_sprites_colors(void)
{
  int count = 0;
  int pen,col,colmask[16];

  unsigned int *pen_usage	=	Machine->gfx[0]->pen_usage;
  int total_elements		=	Machine->gfx[0]->total_elements;

  int max_x				=	Machine->visible_area.max_x;
  int max_y				=	Machine->visible_area.max_y;

  int max_sprites			=	spriteram_size / 8;
  int sprites				=	metro_videoregs[0x0/2] % max_sprites;

  data16_t *src			=	spriteram16 + (sprites - 1) * (8/2);
  data16_t *end			=	spriteram16;

  int color_start			=	((metro_videoregs[0x8/2] & 0xf) << 4 ) + 0x100;

  memset(colmask, 0, sizeof(colmask));

  for ( ; src >= end; src -= 8/2 )
  {
    int x				=	src[ 0 ];
    int y				=	src[ 1 ];
    int attr			=	src[ 2 ];
    int code			=	src[ 3 ];

    int n				=	( ( (attr >> 11) & 0x7 ) + 1 ) *
                  ( ( (attr >>  8) & 0x7 ) + 1 );

    int color			=	( (attr & 0xf0) >> 4 ) ^ 0x0f;

    if ((x & 0xf800) == 0xf800)		continue;

    code				+=	(attr & 0x000f) << 16;

    x					=	(x & 0x07ff) - metro_sprite_xoffs;
    y					=	(y & 0x03ff) - metro_sprite_yoffs;

    if ((x > max_x) || (y > max_y))	continue;

    while (n--)
      colmask[color] |= pen_usage[(code++) % total_elements];
  }

  for (col = 0; col < 16; col++)
    for (pen = 1; pen < 16; pen++)	// pen 0 is transparent
      if (colmask[col] & (1 << pen))
      {
        palette_used_colors[16 * (col + color_start) + pen] = PALETTE_COLOR_USED;
        count++;
      }

//	usrintf_showmessage("%d",count);
}


/***************************************************************************


								Screen Drawing


***************************************************************************/

void metro_tilemap_draw	(struct osd_bitmap *bitmap, struct tilemap *tmap, UINT32 flags, UINT32 priority,
                         int sx, int sy, int wx, int wy)	// scroll & window values
{
#if 1
  tilemap_set_scrollx(tmap, 0, sx - wx + (wx & 7));
  tilemap_set_scrolly(tmap, 0, sy - wy + (wy & 7));
  tilemap_draw(bitmap,tmap, flags, priority);
#else
  int x,y,i;

  /* sub tile placement */
//	sx		=	sx - (wx & ~7) + (wx & 7);
  sx		=	sx - wx;
  sx		=	( (sx & 0x7fff) - (sx & 0x8000) ) % ((WIN_NX-1)*8);

//	sy		=	sy - (wy & ~7) + (wy & 7);
  sy		=	sy - wy;
  sy		=	( (sy & 0x7fff) - (sy & 0x8000) ) % ((WIN_NY-1)*8);

  /* splitting point */
  x		=	(WIN_NX-1)*8 - sx;

  y		=	(WIN_NY-1)*8 - sy;

  for ( i = 0; i < 4 ; i++ )
  {
    struct rectangle clip;

    tilemap_set_scrollx(tmap, 0, sx + ((i & 1) ? -x : 0));
    tilemap_set_scrolly(tmap, 0, sy + ((i & 2) ? -y : 0));

    clip.min_x	=	x - ((i & 1) ? 0 : (WIN_NX-1)*8);
    clip.min_y	=	y - ((i & 2) ? 0 : (WIN_NY-1)*8);

    clip.max_x	=	clip.min_x + (WIN_NX-1)*8 - 1;
    clip.max_y	=	clip.min_y + (WIN_NY-1)*8 - 1;

    if (clip.min_x > Machine->visible_area.max_x)	continue;
    if (clip.min_y > Machine->visible_area.max_y)	continue;

    if (clip.max_x < Machine->visible_area.min_x)	continue;
    if (clip.max_y < Machine->visible_area.min_y)	continue;

    if (clip.min_x < Machine->visible_area.min_x)	clip.min_x = Machine->visible_area.min_x;
    if (clip.max_x > Machine->visible_area.max_x)	clip.max_x = Machine->visible_area.max_x;

    if (clip.min_y < Machine->visible_area.min_y)	clip.min_y = Machine->visible_area.min_y;
    if (clip.max_y > Machine->visible_area.max_y)	clip.max_y = Machine->visible_area.max_y;

    /* The clip region's width must be a multiple of 8!
       This fact renderes the function useless, as far as
       we are concerned! */
    tilemap_set_clip(tmap, &clip);
    tilemap_draw(bitmap,tmap, flags, priority);
  }
#endif
}


static void drawlayer(struct osd_bitmap *bitmap,int layer)
{
  /* Scrolling */
  int sy0		=	metro_scroll[0x0/2];
  int sx0		=	metro_scroll[0x2/2];
  int sy1		=	metro_scroll[0x4/2];
  int sx1		=	metro_scroll[0x6/2];
  int sy2		=	metro_scroll[0x8/2];
  int sx2		=	metro_scroll[0xa/2];

  int wy0		=	metro_window[0x0/2];
  int wx0		=	metro_window[0x2/2];
  int wy1		=	metro_window[0x4/2];
  int wx1		=	metro_window[0x6/2];
  int wy2		=	metro_window[0x8/2];
  int wx2		=	metro_window[0xa/2];


  switch (layer)
  {
    case 0:
      /* Only *one* of tilemap_16x16 & tilemap is enabled at any given time! */
      metro_tilemap_draw(bitmap,tilemap[0], 0, 0, sx0, sy0, wx0, wy0);
      if (tilemap_16x16[0]) metro_tilemap_draw(bitmap,tilemap_16x16[0], 0, 0, sx0, sy0, wx0, wy0);
      break;
    case 1:
      metro_tilemap_draw(bitmap,tilemap[1], 0, 0, sx1, sy1, wx1, wy1);
      if (tilemap_16x16[1]) metro_tilemap_draw(bitmap,tilemap_16x16[1], 0, 0, sx1, sy1, wx1, wy1);
      break;
    case 2:
      metro_tilemap_draw(bitmap,tilemap[2], 0, 0, sx2, sy2, wx2, wy2);
      if (tilemap_16x16[2]) metro_tilemap_draw(bitmap,tilemap_16x16[2], 0, 0, sx2, sy2, wx2, wy2);
      break;
  }
}



/* Dirty tilemaps when the tiles set changes */
static void dirty_tiles(int layer,data16_t *vram,data8_t *dirtyindex)
{
  int col,row;

  for (row = 0; row < WIN_NY; row++)
  {
    for (col = 0; col < WIN_NX; col++)
    {
      int offset = (col + metro_window[layer * 2 + 1] / 8) % BIG_NX +
                   ((row + metro_window[layer * 2 + 0] / 8) % BIG_NY) * BIG_NX;
      data16_t code = vram[offset];

      if (!(code & 0x8000) && dirtyindex[(code & 0x1ff0) >> 4])
      {
        tilemap_mark_tile_dirty(tilemap[layer], row * WIN_NX + col );
        if (tilemap_16x16[layer])
          tilemap_mark_tile_dirty(tilemap_16x16[layer], row * WIN_NX + col );
      }
    }
  }
}


void metro_vh_screenrefresh(struct osd_bitmap *bitmap,int full_refresh)
{
  int i,sprite_pri,layers_ctrl = -1;
  data8_t *dirtyindex;
  data16_t screenctrl = *metro_screenctrl;


  dirtyindex = malloc(metro_tiletable_size/4);
  if (dirtyindex)
  {
    int dirty = 0;

    memset(dirtyindex,0,metro_tiletable_size/4);
    for (i = 0; i < metro_tiletable_size/4; i++)
    {
      UINT32 tile_new = (metro_tiletable[2*i + 0] << 16 ) + metro_tiletable[2*i + 1];
      UINT32 tile_old = (metro_tiletable_old[2*i + 0] << 16 ) + metro_tiletable_old[2*i + 1];

      if ((tile_new ^ tile_old) & 0x0fffffff)
      {
        dirtyindex[i] = 1;
        dirty = 1;
      }
    }
    memcpy(metro_tiletable_old,metro_tiletable,metro_tiletable_size);

    if (dirty)
    {
      dirty_tiles(0,metro_vram_0,dirtyindex);
      dirty_tiles(1,metro_vram_1,dirtyindex);
      dirty_tiles(2,metro_vram_2,dirtyindex);
    }
    free(dirtyindex);
  }

  metro_sprite_xoffs	=	metro_videoregs[0x06/2] - Machine->drv->screen_width  / 2;
  metro_sprite_yoffs	=	metro_videoregs[0x04/2] - Machine->drv->screen_height / 2;

  /* Black background color ? */
  fillbitmap(bitmap,palette_transparent_pen,&Machine->visible_area);


  /*	Screen Control Register:

  	f--- ---- ---- ----		?
  	-edc b--- ---- ----
  	---- -a98 ---- ----		?
  	---- ---- 7654 32--
  	---- ---- ---- --1-		? Blank Screen
  	---- ---- ---- ---0		Flip  Screen	*/
  if (screenctrl & 2)	return;
  flip_screen_set(screenctrl & 1);

  /* If the game supports 16x16 tiles, make sure that the
     16x16 and 8x8 tilemaps of a given layer are not simultaneously
     enabled! */
  if (support_16x16)
  {
    int layer;

    for (layer = 0; layer < 3; layer++)
    {
      int big = screenctrl & (0x0020 << layer);

      tilemap_set_enable(tilemap[layer],!big);
      tilemap_set_enable(tilemap_16x16[layer],big);
    }
  }


#ifdef MAME_DEBUG
  if (keyboard_pressed(KEYCODE_Z))
  {
    int msk = 0;
    if (keyboard_pressed(KEYCODE_Q))	msk |= 1;
    if (keyboard_pressed(KEYCODE_W))	msk |= 2;
    if (keyboard_pressed(KEYCODE_E))	msk |= 4;
    if (keyboard_pressed(KEYCODE_A))	msk |= 8;
    if (msk != 0) layers_ctrl &= msk;

    usrintf_showmessage("l %x-%x-%x r %04x %04x %04x",
                        (metro_videoregs[0x10/2]&0x30)>>4,(metro_videoregs[0x10/2]&0xc)>>2,metro_videoregs[0x10/2]&3,
                        metro_videoregs[0x02/2], metro_videoregs[0x12/2],
                        *metro_screenctrl);
  }
#endif

  tilemap_update(ALL_TILEMAPS);

  if (!support_8bpp)
  {
    palette_init_used_colors();

    metro_mark_sprites_colors();
  }

  palette_recalc();

  if (has_zoom) K053936_zoom_draw(bitmap);

  /* Sprite placement wrt layers: 3..0 (3 is below every layer,0 on top) */
  sprite_pri = (metro_videoregs[0x02/2] & 0x0300) >> 8;

  if ((layers_ctrl & 8) && (sprite_pri == 3))	for (i = 0; i < 0x20; i++)	metro_draw_sprites(bitmap, i);
  if (layers_ctrl & 1)	drawlayer(bitmap,(metro_videoregs[0x10/2] & 0x30)>>4);
  if ((layers_ctrl & 8) && (sprite_pri == 2))	for (i = 0; i < 0x20; i++)	metro_draw_sprites(bitmap, i);
  if (layers_ctrl & 2)	drawlayer(bitmap,(metro_videoregs[0x10/2] & 0x0c)>>2);
  if ((layers_ctrl & 8) && (sprite_pri == 1))	for (i = 0; i < 0x20; i++)	metro_draw_sprites(bitmap, i);
  if (layers_ctrl & 4)	drawlayer(bitmap,(metro_videoregs[0x10/2] & 0x03));
  if ((layers_ctrl & 8) && (sprite_pri == 0))	for (i = 0; i < 0x20; i++)	metro_draw_sprites(bitmap, i);
}
