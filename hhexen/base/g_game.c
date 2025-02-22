
//**************************************************************************
//**
//** g_game.c : Heretic 2 : Raven Software, Corp.
//**
//** $Revision: 584 $
//** $Date: 2012-02-17 12:01:51 +0200 (Fri, 17 Feb 2012) $
//**
//**************************************************************************

#include "h2stdinc.h"
#include "h2def.h"
#include "p_local.h"
#include "soundst.h"

#define AM_STARTKEY	9

// External functions

extern void R_InitSky(int map);
extern void P_PlayerNextArtifact(player_t *player);

// Functions

boolean G_CheckDemoStatus (void);
static void G_ReadDemoTiccmd (ticcmd_t *cmd);
static void G_WriteDemoTiccmd (ticcmd_t *cmd);

void G_InitNew (skill_t skill, int episode, int map);

static void G_DoReborn (int playernum);

static void G_DoLoadLevel(void);
static void G_DoInitNew(void);
static void G_DoNewGame(void);

void G_DoLoadGame(void);
static void G_DoPlayDemo(void);
static void G_DoTeleportNewMap(void);
static void G_DoCompleted(void);
static void G_DoWorldDone(void);
static void G_DoSaveGame(void);
static void G_DoSingleReborn(void);

void H2_PageTicker(void);
void H2_AdvanceDemo(void);

extern boolean mn_SuicideConsole;

gameaction_t	gameaction;
gamestate_t	gamestate;
skill_t		gameskill;
int		gameepisode;
int		gamemap;
int		prevmap;

boolean		paused;

boolean		usergame;		// ok to save / end game

static boolean	sendpause;		// send a pause event next tic
static boolean	sendsave;		// send a save event next tic

static boolean	timingdemo;		// if true, exit with report on completion
static int	starttime;		// for comparative timing purposes      

boolean		viewactive;

boolean		deathmatch;		// only if started as net death
boolean		netgame;		// only true if packets are broadcast
boolean		playeringame[MAXPLAYERS];
player_t	players[MAXPLAYERS];
pclass_t	PlayerClasses[MAXPLAYERS];

// Position indicator for cooperative net-play reborn
int		RebornPosition;

int		consoleplayer;		// player taking events and displaying
int		displayplayer;		// view being displayed
int		gametic;
int		levelstarttic;		// gametic at level start

boolean		demorecording;
boolean		demoplayback;
boolean		singledemo;		// quit after playing a demo from cmdline
static byte	*demobuffer, *demo_p;
static char	demoname[MAX_OSPATH];

static short	consistancy[MAXPLAYERS][BACKUPTICS];

boolean		precache = true;	// if true, load all graphics at start

//
// controls (have defaults)
//
int	key_right, key_left, key_up, key_down;
int	key_strafeleft, key_straferight, key_jump;
int	key_fire, key_use, key_strafe, key_speed;
int	key_flyup, key_flydown, key_flycenter;
int	key_lookup, key_lookdown, key_lookcenter;
int	key_invleft, key_invright, key_useartifact;

int	mouselook;
int	alwaysrun;	/* boolean */

int	mousebfire;
int	mousebstrafe;
int	mousebforward;
int	mousebjump;

int	joybfire;
int	joybstrafe;
int	joybuse;
int	joybspeed;
int	joybjump;

int	LeaveMap;
static int LeavePosition;

//#define MAXPLMOVE       0x32 // Old Heretic Max move

static fixed_t MaxPlayerMove[NUMCLASSES] =
{
		0x3C,
		0x32,
		0x2D,
#ifdef ASSASSIN
		0x3D,
#endif
		0x31
};

static fixed_t forwardmove[NUMCLASSES][2] =
{
	{ 0x1D, 0x3C },
	{ 0x19, 0x32 },
	{ 0x16, 0x2E },
#ifdef ASSASSIN
	{ 0x17, 0x3D },
#endif
	{ 0x18, 0x31 }
};

static fixed_t sidemove[NUMCLASSES][2] =
{
	{ 0x1B, 0x3B },
	{ 0x18, 0x28 },
	{ 0x15, 0x25 },
#ifdef ASSASSIN
	{ 0x16, 0x3C },
#endif
	{ 0x17, 0x27 }
};

static fixed_t	angleturn[3] = {640, 1280, 320};	// + slow turn
#define SLOWTURNTICS		6

boolean	gamekeydown[MAXKEYS];
static int	turnheld;			// for accelerative turning
static int	lookheld;

static boolean	mousearray[4];
static boolean	*mousebuttons = &mousearray[1];	// allow [-1]

static int	mousex, mousey;			// mouse values are used once
static int	dclicktime, dclickstate, dclicks;
static int	dclicktime2, dclickstate2, dclicks2;

static int	joyxmove, joyymove;		// joystick values are repeated
static boolean	joyarray[5];
static boolean	*joybuttons = &joyarray[1];	// allow [-1]

static int	loadgameslot;
static int	savegameslot;
static char	savedescription[32];

static int	inventoryTics;

boolean		usearti = true;

static skill_t	TempSkill;
static int	TempEpisode;
static int	TempMap;

//=============================================================================

/*
====================
=
= G_BuildTiccmd
=
= Builds a ticcmd from all of the available inputs or reads it from the
= demo buffer.
= If recording a demo, write it out
====================
*/

extern boolean inventory;
extern boolean artiskip;
extern int curpos;
extern int inv_ptr;

void G_BuildTiccmd (ticcmd_t *cmd)
{
	int	i;
	boolean	strafe, bstrafe;
	int	speed, tspeed, lspeed;
	int	forward, side;
	int	look, arti;
	int	flyheight;
	int	pClass;

	pClass = players[consoleplayer].playerclass;
	memset (cmd, 0, sizeof(*cmd));

//	cmd->consistancy =
//		consistancy[consoleplayer][(maketic*ticdup)%BACKUPTICS];

	cmd->consistancy =
		consistancy[consoleplayer][maketic%BACKUPTICS];

//printf ("cons: %i\n",cmd->consistancy);
	strafe = gamekeydown[key_strafe] || mousebuttons[mousebstrafe] || joybuttons[joybstrafe];
	speed = gamekeydown[key_speed] || joybuttons[joybspeed] || joybuttons[joybspeed];
	if (alwaysrun && !demoplayback && !demorecording)
		speed = !speed;
	forward = side = look = arti = flyheight = 0;

//
// use two stage accelerative turning on the keyboard and joystick
//
	if (joyxmove < 0 || joyxmove > 0 || gamekeydown[key_right] || gamekeydown[key_left])
		turnheld += ticdup;
	else
		turnheld = 0;
	if (turnheld < SLOWTURNTICS)
		tspeed = 2;		// slow turn
	else
		tspeed = speed;

	if (gamekeydown[key_lookdown] || gamekeydown[key_lookup])
	{
		lookheld += ticdup;
	}
	else
	{
		lookheld = 0;
	}
	if (lookheld < SLOWTURNTICS)
	{
		lspeed = 1; // 3;
	}
	else
	{
		lspeed = 2; // 5;
	}

//
// let movement keys cancel each other out
//
	if (strafe)
	{
		if (gamekeydown[key_right])
		{
			side += sidemove[pClass][speed];
		}
		if (gamekeydown[key_left])
		{
			side -= sidemove[pClass][speed];
		}
		if (joyxmove > 0)
		{
			side += sidemove[pClass][speed];
		}
		if (joyxmove < 0)
		{
			side -= sidemove[pClass][speed];
		}
	}
	else
	{
		if (gamekeydown[key_right])
			cmd->angleturn -= angleturn[tspeed];
		if (gamekeydown[key_left])
			cmd->angleturn += angleturn[tspeed];
		if (joyxmove > 0)
			cmd->angleturn -= angleturn[tspeed];
		if (joyxmove < 0)
			cmd->angleturn += angleturn[tspeed];
	}

	if (gamekeydown[key_up])
	{
		forward += forwardmove[pClass][speed];
	}
	if (gamekeydown[key_down])
	{
		forward -= forwardmove[pClass][speed];
	}
	if (joyymove < 0)
	{
		forward += forwardmove[pClass][speed];
	}
	if (joyymove > 0)
	{
		forward -= forwardmove[pClass][speed];
	}
	if (gamekeydown[key_straferight])
	{
		side += sidemove[pClass][speed];
	}
	if (gamekeydown[key_strafeleft])
	{
		side -= sidemove[pClass][speed];
	}

	// Look up/down/center keys
	if (gamekeydown[key_lookup])
	{
		look = lspeed;
	}
	if (gamekeydown[key_lookdown])
	{
		look = -lspeed;
	}
	if (gamekeydown[key_lookcenter])
	{
		look = TOCENTER;
	}

	// Fly up/down/drop keys
	if (gamekeydown[key_flyup])
	{
		flyheight = 5; // note that the actual flyheight will be twice this
	}
	if (gamekeydown[key_flydown])
	{
		flyheight = -5;
	}
	if (gamekeydown[key_flycenter])
	{
		flyheight = TOCENTER;
		look = TOCENTER;
	}
	// Use artifact key
	if (gamekeydown[key_useartifact])
	{
		if (gamekeydown[key_speed] && artiskip)
		{
			if (players[consoleplayer].inventory[inv_ptr].type != arti_none)
			{ // Skip an artifact
				gamekeydown[key_useartifact] = false;
				P_PlayerNextArtifact(&players[consoleplayer]);
			}
		}
		else
		{
			if (inventory)
			{
				players[consoleplayer].readyArtifact =
					players[consoleplayer].inventory[inv_ptr].type;
				inventory = false;
				cmd->arti = 0;
				usearti = false;
			}
			else if (usearti)
			{
				cmd->arti |= 
					players[consoleplayer].inventory[inv_ptr].type&AFLAG_MASK;
				usearti = false;
			}
		}
	}
	if (gamekeydown[key_jump] || mousebuttons[mousebjump] || joybuttons[joybjump])
	{
		cmd->arti |= AFLAG_JUMP;
	}
	if (mn_SuicideConsole)
	{
		cmd->arti |= AFLAG_SUICIDE;
		mn_SuicideConsole = false;
	}

	// Artifact hot keys
	if (gamekeydown[KEY_BACKSPACE] && !cmd->arti)
	{
		gamekeydown[KEY_BACKSPACE] = false;	// Use one of each artifact
		cmd->arti = NUMARTIFACTS;
	}
	else if (gamekeydown[KEY_BACKSLASH] && !cmd->arti && (players[consoleplayer].mo->health < MAXHEALTH))
	{
		gamekeydown[KEY_BACKSLASH] = false;
		cmd->arti = arti_health;
	}
	else if (gamekeydown[KEY_ZERO] && !cmd->arti)
	{
		gamekeydown[KEY_ZERO] = false;
		cmd->arti = arti_poisonbag;
	}
	else if (gamekeydown[KEY_NINE] && !cmd->arti)
	{
		gamekeydown[KEY_NINE] = false;
		cmd->arti = arti_blastradius;
	}
	else if (gamekeydown[KEY_EIGHT] && !cmd->arti)
	{
		gamekeydown[KEY_EIGHT] = false;
		cmd->arti = arti_teleport;
	}
	else if (gamekeydown[KEY_SEVEN] && !cmd->arti)
	{
		gamekeydown[KEY_SEVEN] = false;
		cmd->arti = arti_teleportother;
	}
	else if (gamekeydown[KEY_SIX] && !cmd->arti)
	{
		gamekeydown[KEY_SIX] = false;
		cmd->arti = arti_egg;
	}
	else if (gamekeydown[KEY_FIVE] && !cmd->arti && !players[consoleplayer].powers[pw_invulnerability])
	{
		gamekeydown[KEY_FIVE] = false;
		cmd->arti = arti_invulnerability;
	}

//
// buttons
//
	cmd->chatchar = CT_dequeueChatChar();
	if (gamekeydown[key_fire] || mousebuttons[mousebfire]
				  || joybuttons[joybfire])
		cmd->buttons |= BT_ATTACK;

	if (gamekeydown[key_use] || joybuttons[joybuse] )
	{
		cmd->buttons |= BT_USE;
		dclicks = 0;		// clear double clicks if hit use button
	}

	for (i = 0; i < NUMWEAPONS; i++)
	{
		if (gamekeydown['1'+i])
		{
			cmd->buttons |= BT_CHANGE;
			cmd->buttons |= i<<BT_WEAPONSHIFT;
			break;
		}
	}

//
// mouse
//
#if 0
	printf ("%d %d %d %d <%d %d>\n",
		mousebuttons[mousebfire], mousebuttons[mousebforward],
		mousebuttons[mousebjump], mousebuttons[mousebstrafe],
		mousex, mousey);
#endif
	if (mousebuttons[mousebforward])
	{
		forward += forwardmove[pClass][speed];
	}

//
// forward double click
//
	if (mousebuttons[mousebforward] != dclickstate && dclicktime > 1)
	{
		dclickstate = mousebuttons[mousebforward];
		if (dclickstate)
			dclicks++;
		if (dclicks == 2)
		{
			cmd->buttons |= BT_USE;
			dclicks = 0;
		}
		else
			dclicktime = 0;
	}
	else
	{
		dclicktime += ticdup;
		if (dclicktime > 20)
		{
			dclicks = 0;
			dclickstate = 0;
		}
	}

//
// strafe double click
//
	bstrafe = mousebuttons[mousebstrafe] || joybuttons[joybstrafe];
	if (bstrafe != dclickstate2 && dclicktime2 > 1)
	{
		dclickstate2 = bstrafe;
		if (dclickstate2)
			dclicks2++;
		if (dclicks2 == 2)
		{
			cmd->buttons |= BT_USE;
			dclicks2 = 0;
		}
		else
			dclicktime2 = 0;
	}
	else
	{
		dclicktime2 += ticdup;
		if (dclicktime2 > 20)
		{
			dclicks2 = 0;
			dclickstate2 = 0;
		}
	}
	if (strafe)
	{
		side += mousex*2;
	}
	else
	{
		cmd->angleturn -= mousex*0x8;
	}

	if (demorecording || demoplayback || (mouselook == 0))
	{
		forward += mousey;
	}
	else if (mousey && !paused)	/* mouselook, but not when paused */
	{
		/* We'll directly change the viewing pitch of the console player. */
		float adj = ((mousey*0x4) << 16) / (float) ANGLE_180*180*110.0/85.0;
		float newlookdir = 0; /* jim initialiser added to prevent warning */

		adj *= 2;	/* Speed up the X11 mlook a little. */

		if (mouselook == 1)
			newlookdir = players[consoleplayer].lookdir + adj;
		else if (mouselook == 2)
			newlookdir = players[consoleplayer].lookdir - adj;

		// vertical view angle taken from p_user.c line 249.
		if (newlookdir > 90)
			newlookdir = 90;
		else if (newlookdir < -110)
			newlookdir = -110;

		players[consoleplayer].lookdir = newlookdir;
	}

	mousex = mousey = 0;

	if (forward > MaxPlayerMove[pClass])
	{
		forward = MaxPlayerMove[pClass];
	}
	else if (forward < -MaxPlayerMove[pClass])
	{
		forward = -MaxPlayerMove[pClass];
	}
	if (side > MaxPlayerMove[pClass])
	{
		side = MaxPlayerMove[pClass];
	}
	else if (side < -MaxPlayerMove[pClass])
	{
		side = -MaxPlayerMove[pClass];
	}
	if (players[consoleplayer].powers[pw_speed] && !players[consoleplayer].morphTics)
	{ // Adjust for a player with a speed artifact
		forward = (3*forward)>>1;
		side = (3*side)>>1;
	}
	cmd->forwardmove += forward;
	cmd->sidemove += side;
	if (players[consoleplayer].playerstate == PST_LIVE)
	{
		if (look < 0)
		{
			look += 16;
		}
		cmd->lookfly = look;
	}
	if (flyheight < 0)
	{
		flyheight += 16;
	}
	cmd->lookfly |= flyheight<<4;

//
// special buttons
//
	if (sendpause)
	{
		sendpause = false;
		cmd->buttons = BT_SPECIAL | BTS_PAUSE;
	}
	if (sendsave)
	{
		sendsave = false;
		cmd->buttons = BT_SPECIAL | BTS_SAVEGAME | (savegameslot<<BTS_SAVESHIFT);
	}
}


/*
==============
=
= G_DoLoadLevel
=
==============
*/

static void G_DoLoadLevel (void)
{
	int		i;

	levelstarttic = gametic;	// for time calculation
	gamestate = GS_LEVEL;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].playerstate == PST_DEAD)
			players[i].playerstate = PST_REBORN;
		memset (players[i].frags, 0, sizeof(players[i].frags));
	}

	SN_StopAllSequences();
	P_SetupLevel (gameepisode, gamemap, 0, gameskill);
	displayplayer = consoleplayer;	// view the guy you are playing
	starttime = I_GetTime ();
	gameaction = ga_nothing;
	Z_CheckHeap ();

//
// clear cmd building stuff
//
	memset (gamekeydown, 0, sizeof(gamekeydown));
	joyxmove = joyymove = 0;
	mousex = mousey = 0;
	sendpause = sendsave = paused = false;
//	memset (mousebuttons, 0, sizeof(mousebuttons));
//	memset (joybuttons, 0, sizeof(joybuttons));
	memset (joyarray, 0, sizeof(joyarray));
	memset (mousearray, 0, sizeof(mousearray));
}


/*
===============================================================================
=
= G_Responder 
=
= get info needed to make ticcmd_ts for the players
=
===============================================================================
*/

boolean G_Responder(event_t *ev)
{
	player_t *plr;
	extern boolean MenuActive;

	plr = &players[consoleplayer];
	if (ev->type == ev_keyup && ev->data1 == key_useartifact)
	{ // flag to denote that it's okay to use an artifact
		if (!inventory)
		{
			plr->readyArtifact = plr->inventory[inv_ptr].type;
		}
		usearti = true;
	}

	// Check for spy mode player cycle
	if  (gamestate == GS_LEVEL && ev->type == ev_keydown
			&& ev->data1 == KEY_F12 && !deathmatch)
	{ // Cycle the display player
		do
		{
			displayplayer++;
			if (displayplayer == MAXPLAYERS)
			{
				displayplayer = 0;
			}
		}
		while (!playeringame[displayplayer] && displayplayer != consoleplayer);

		return true;
	}

	if (CT_Responder(ev))
	{ // Chat ate the event
		return true;
	}
	if (gamestate == GS_LEVEL)
	{
		if (SB_Responder(ev))
		{ // Status bar ate the event
			return true;
		}
		if (AM_Responder(ev))
		{ // Automap ate the event
			return true;
		}
	}

	switch (ev->type)
	{
	case ev_keydown:
		if (ev->data1 == key_invleft)
		{
			inventoryTics = 5*35;
			if (!inventory)
			{
				inventory = true;
				break;
			}
			inv_ptr--;
			if (inv_ptr < 0)
			{
				inv_ptr = 0;
			}
			else
			{
				curpos--;
				if (curpos < 0)
				{
					curpos = 0;
				}
			}
			return true;
		}
		if (ev->data1 == key_invright)
		{
			inventoryTics = 5*35;
			if (!inventory)
			{
				inventory = true;
				break;
			}
			inv_ptr++;
			if (inv_ptr >= plr->inventorySlotNum)
			{
				inv_ptr--;
				if (inv_ptr < 0)
					inv_ptr = 0;
			}
			else
			{
				curpos++;
				if (curpos > 6)
				{
					curpos = 6;
				}
			}
			return true;
		}
		if (ev->data1 == KEY_PAUSE)
		{
			if (!MenuActive && gamestate != GS_FINALE)
				sendpause = true;
			return true;
		}
		if (ev->data1 < MAXKEYS)
		{
			gamekeydown[ev->data1] = true;
		}
		return true;	// eat key down events

	case ev_keyup:
		if (ev->data1 < MAXKEYS)
		{
			gamekeydown[ev->data1] = false;
		}
		return false;	// always let key up events filter down

	case ev_mouse:
		mousebuttons[0] = ev->data1 & 1;
		mousebuttons[1] = ev->data1 & 2;
		mousebuttons[2] = ev->data1 & 4;
		mousex = ev->data2 * (mouseSensitivity + 5) / 10;
		mousey = ev->data3 * (mouseSensitivity + 5) / 10;
		return true;	// eat events

	case ev_joystick:
		joybuttons[0] = ev->data1 & 1;
		joybuttons[1] = ev->data1 & 2;
		joybuttons[2] = ev->data1 & 4;
		joybuttons[3] = ev->data1 & 8;
		joyxmove = ev->data2;
		joyymove = ev->data3;
		return true;	// eat events

	default:
		break;
	}
	return false;
}


//==========================================================================
//
// G_Ticker
//
//==========================================================================

void G_Ticker(void)
{
	int		i, buf;
	ticcmd_t	*cmd = NULL;

//
// do player reborns if needed
//
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].playerstate == PST_REBORN)
			G_DoReborn (i);
	}

//
// do things to change the game state
//
	while (gameaction != ga_nothing)
	{
		switch (gameaction)
		{
		case ga_loadlevel:
			G_DoLoadLevel();
			break;
		case ga_initnew:
			G_DoInitNew();
			break;
		case ga_newgame:
			G_DoNewGame();
			break;
		case ga_loadgame:
			Draw_LoadIcon();
			G_DoLoadGame();
			break;
		case ga_savegame:
			Draw_SaveIcon();
			G_DoSaveGame();
			break;
		case ga_singlereborn:
			G_DoSingleReborn();
			break;
		case ga_playdemo:
			G_DoPlayDemo();
			break;
		case ga_screenshot:
			M_ScreenShot();
			gameaction = ga_nothing;
			break;
		case ga_leavemap:
			Draw_TeleportIcon();
			G_DoTeleportNewMap();
			break;
		case ga_completed:
			G_DoCompleted();
			break;
		case ga_worlddone:
			G_DoWorldDone();
			break;
		case ga_victory:
			F_StartFinale();
			break;
		default:
			break;
		}
	}

//
// get commands, check consistancy, and build new consistancy check
//
	//buf = gametic % BACKUPTICS;
	buf = (gametic / ticdup) % BACKUPTICS;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			cmd = &players[i].cmd;

			memcpy (cmd, &netcmds[i][buf], sizeof(ticcmd_t));

			if (demoplayback)
				G_ReadDemoTiccmd (cmd);
			if (demorecording)
				G_WriteDemoTiccmd (cmd);

			if (netgame && !(gametic%ticdup))
			{
				if (gametic > BACKUPTICS && consistancy[i][buf] != cmd->consistancy)
				{
					I_Error ("consistency failure (%i should be %i)",cmd->consistancy, consistancy[i][buf]);
				}
				if (players[i].mo)
					consistancy[i][buf] = players[i].mo->x;
				else
					consistancy[i][buf] = rndindex;
			}
		}
	}

//
// check for special buttons
//
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			if (players[i].cmd.buttons & BT_SPECIAL)
			{
				switch (players[i].cmd.buttons & BT_SPECIALMASK)
				{
				case BTS_PAUSE:
					paused ^= 1;
					if (paused)
					{
						S_PauseSound();
					}
					else
					{
						S_ResumeSound();
					}
					break;

				case BTS_SAVEGAME:
					if (!savedescription[0])
					{
						if (netgame)
						{
							strcpy (savedescription, "NET GAME");
						}
						else
						{
							strcpy(savedescription, "SAVE GAME");
						}
					}
					savegameslot = 
						(players[i].cmd.buttons & BTS_SAVEMASK)>>BTS_SAVESHIFT;
					gameaction = ga_savegame;
					break;
				}
			}
		}
	}

// turn inventory off after a certain amount of time
	if (inventory && !(--inventoryTics))
	{
		players[consoleplayer].readyArtifact =
			players[consoleplayer].inventory[inv_ptr].type;
		inventory = false;
		cmd->arti = 0;
	}

//
// do main actions
//
	switch (gamestate)
	{
	case GS_LEVEL:
		P_Ticker ();
		SB_Ticker ();
		AM_Ticker ();
		CT_Ticker();
		break;
	case GS_INTERMISSION:
		IN_Ticker ();
		break;
	case GS_FINALE:
		F_Ticker();
		break;
	case GS_DEMOSCREEN:
		H2_PageTicker ();
		break;
	}
}


/*
==============================================================================

						PLAYER STRUCTURE FUNCTIONS

also see P_SpawnPlayer in P_Things
==============================================================================
*/

//==========================================================================
//
// G_PlayerExitMap
//
// Called when the player leaves a map.
//
//==========================================================================

void G_PlayerExitMap(int playerNumber)
{
	int i;
	player_t *player;
	int flightPower;

	player = &players[playerNumber];

//	if (deathmatch)
//	{
//		// Strip all but one of each type of artifact
//		for (i = 0; i < player->inventorySlotNum; i++)
//		{
//			player->inventory[i].count = 1;
//		}
//		player->artifactCount = player->inventorySlotNum;
//	}
//	else

	// Strip all current powers (retain flight)
	flightPower = player->powers[pw_flight];
	memset(player->powers, 0, sizeof(player->powers));
	player->powers[pw_flight] = flightPower;

	if (deathmatch)
	{
		player->powers[pw_flight] = 0;
	}
	else
	{
		if (P_GetMapCluster(gamemap) != P_GetMapCluster(LeaveMap))
		{ // Entering new cluster
			// Strip all keys
			player->keys = 0;

			// Strip flight artifact
			for (i = 0; i < 25; i++)
			{
				player->powers[pw_flight] = 0;
				P_PlayerUseArtifact(player, arti_fly);
			}
			player->powers[pw_flight] = 0;
		}
	}

	if (player->morphTics)
	{
		player->readyweapon = player->mo->special1;	// Restore weapon
		player->morphTics = 0;
	}
	player->messageTics = 0;
	player->lookdir = 0;
	player->mo->flags &= ~MF_SHADOW;	// Remove invisibility
	player->extralight = 0;			// Remove weapon flashes
	player->fixedcolormap = 0;		// Remove torch
	player->damagecount = 0;		// No palette changes
	player->bonuscount = 0;
	player->poisoncount = 0;
	if (player == &players[consoleplayer])
	{
		SB_state = -1;			// refresh the status bar
		viewangleoffset = 0;
	}
}

//==========================================================================
//
// G_PlayerReborn
//
// Called after a player dies.  Almost everything is cleared and
// initialized.
//
//==========================================================================

void G_PlayerReborn(int player)
{
	player_t *p;
	int frags[MAXPLAYERS];
	int killcount, itemcount, secretcount;
	unsigned int worldTimer;

	memcpy(frags, players[player].frags, sizeof(frags));
	killcount = players[player].killcount;
	itemcount = players[player].itemcount;
	secretcount = players[player].secretcount;
	worldTimer = players[player].worldTimer;

	p = &players[player];
	memset(p, 0, sizeof(*p));

	memcpy(players[player].frags, frags, sizeof(players[player].frags));
	players[player].killcount = killcount;
	players[player].itemcount = itemcount;
	players[player].secretcount = secretcount;
	players[player].worldTimer = worldTimer;
	players[player].playerclass = PlayerClasses[player];

	p->usedown = p->attackdown = true;	// don't do anything immediately
	p->playerstate = PST_LIVE;
	p->health = MAXHEALTH;
	p->readyweapon = p->pendingweapon = WP_FIRST;
	p->weaponowned[WP_FIRST] = true;
	p->messageTics = 0;
	p->lookdir = 0;
	localQuakeHappening[player] = false;
	if (p == &players[consoleplayer])
	{
		SB_state = -1;	// refresh the status bar
		inv_ptr = 0;	// reset the inventory pointer
		curpos = 0;
		viewangleoffset = 0;
	}
}

/*
====================
=
= G_CheckSpot
=
= Returns false if the player cannot be respawned at the given mapthing_t spot
= because something is occupying it
====================
*/

void P_SpawnPlayer (mapthing_t *mthing);

boolean G_CheckSpot (int playernum, mapthing_t *mthing)
{
	fixed_t		x, y;
	subsector_t	*ss;
	unsigned int	an;
	mobj_t		*mo;

	x = mthing->x << FRACBITS;
	y = mthing->y << FRACBITS;

	players[playernum].mo->flags2 &= ~MF2_PASSMOBJ;
	if (! P_CheckPosition(players[playernum].mo, x, y))
	{
		players[playernum].mo->flags2 |= MF2_PASSMOBJ;
		return false;
	}
	players[playernum].mo->flags2 |= MF2_PASSMOBJ;

// spawn a teleport fog
	ss = R_PointInSubsector (x, y);
	an = (ANG45 * (mthing->angle / 45)) >> ANGLETOFINESHIFT;

	mo = P_SpawnMobj (x + 20*finecosine[an], y + 20*finesine[an],
		ss->sector->floorheight + TELEFOGHEIGHT, MT_TFOG);

	if (players[consoleplayer].viewz != 1)
		S_StartSound (mo, SFX_TELEPORT);	// don't start sound on first frame

	return true;
}

/*
====================
=
= G_DeathMatchSpawnPlayer
=
= Spawns a player at one of the random death match spots
= called at level load and each death
====================
*/

void G_DeathMatchSpawnPlayer (int playernum)
{
	int		i, j;
	int		selections;

	selections = deathmatch_p - deathmatchstarts;

	// This check has been moved to p_setup.c:P_LoadThings()
	//if (selections < 8)
	//	I_Error ("Only %i deathmatch spots, 8 required", selections);

	for (j = 0; j < 20; j++)
	{
		i = P_Random() % selections;
		if (G_CheckSpot (playernum, &deathmatchstarts[i]))
		{
			deathmatchstarts[i].type = playernum + 1;
			P_SpawnPlayer (&deathmatchstarts[i]);
			return;
		}
	}

// no good spot, so the player will probably get stuck
	P_SpawnPlayer (&playerstarts[0][playernum]);
}

//==========================================================================
//
// G_DoReborn
//
//==========================================================================

static void G_DoReborn(int playernum)
{
	int i;
	boolean oldWeaponowned[NUMWEAPONS];
	int oldKeys;
	int oldPieces;
	boolean foundSpot;
	int bestWeapon;

	if (G_CheckDemoStatus())
	{
		return;
	}
	if (!netgame)
	{
		if (SV_RebornSlotAvailable())
		{ // Use the reborn code if the slot is available
			gameaction = ga_singlereborn;
		}
		else
		{ // Start a new game if there's no reborn info
			gameaction = ga_newgame;
		}
	}
	else
	{ // Net-game
		players[playernum].mo->player = NULL;	// Dissassociate the corpse

		if (deathmatch)
		{ // Spawn at random spot if in death match
			G_DeathMatchSpawnPlayer(playernum);
			return;
		}

		// Cooperative net-play, retain keys and weapons
		oldKeys = players[playernum].keys;
		oldPieces = players[playernum].pieces;
		for (i = 0; i < NUMWEAPONS; i++)
		{
			oldWeaponowned[i] = players[playernum].weaponowned[i];
		}

		foundSpot = false;
		if (G_CheckSpot(playernum, &playerstarts[RebornPosition][playernum]))
		{ // Appropriate player start spot is open
			P_SpawnPlayer(&playerstarts[RebornPosition][playernum]);
			foundSpot = true;
		}
		else
		{
			// Try to spawn at one of the other player start spots
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (G_CheckSpot(playernum, &playerstarts[RebornPosition][i]))
				{ // Found an open start spot
					// Fake as other player
					playerstarts[RebornPosition][i].type = playernum + 1;
					P_SpawnPlayer(&playerstarts[RebornPosition][i]);

					// Restore proper player type
					playerstarts[RebornPosition][i].type = i + 1;

					foundSpot = true;
					break;
				}
			}
		}

		if (foundSpot == false)
		{ // Player's going to be inside something
			P_SpawnPlayer(&playerstarts[RebornPosition][playernum]);
		}

		// Restore keys and weapons
		players[playernum].keys = oldKeys;
		players[playernum].pieces = oldPieces;
		for (bestWeapon = 0, i = 0; i < NUMWEAPONS; i++)
		{
			if (oldWeaponowned[i])
			{
				bestWeapon = i;
				players[playernum].weaponowned[i] = true;
			}
		}
		players[playernum].mana[MANA_1] = 25;
		players[playernum].mana[MANA_2] = 25;
		if (bestWeapon)
		{ // Bring up the best weapon
			players[playernum].pendingweapon = bestWeapon;
		}
	}
}


void G_ScreenShot (void)
{
	gameaction = ga_screenshot;
}


//==========================================================================
//
// G_StartNewInit
//
//==========================================================================

void G_StartNewInit(void)
{
	SV_InitBaseSlot();
	SV_ClearRebornSlot();
	P_ACSInitNewGame();
	// Default the player start spot group to 0
	RebornPosition = 0;
}

//==========================================================================
//
// G_StartNewGame
//
//==========================================================================

void G_StartNewGame(skill_t skill)
{
	int realMap;

	G_StartNewInit();
	realMap = P_TranslateMap(1);
	if (realMap == -1)
	{
		realMap = 1;
	}
	G_InitNew(TempSkill, 1, realMap);
}

//==========================================================================
//
// G_TeleportNewMap
//
// Only called by the warp cheat code.  Works just like normal map to map
// teleporting, but doesn't do any interlude stuff.
//
//==========================================================================

void G_TeleportNewMap(int map, int position)
{
	gameaction = ga_leavemap;
	LeaveMap = map;
	LeavePosition = position;
}

//==========================================================================
//
// G_DoTeleportNewMap
//
//==========================================================================

static void G_DoTeleportNewMap(void)
{
	SV_MapTeleport(LeaveMap, LeavePosition);
	gamestate = GS_LEVEL;
	gameaction = ga_nothing;
	RebornPosition = LeavePosition;
}

/*
boolean secretexit;
void G_ExitLevel (void)
{
	secretexit = false;
	gameaction = ga_completed;
}
void G_SecretExitLevel (void)
{
	secretexit = true;
	gameaction = ga_completed;
}
*/

//==========================================================================
//
// G_Completed
//
// Starts intermission routine, which is used only during hub exits,
// and DeathMatch games.
//==========================================================================

void G_Completed(int map, int position)
{
	if (shareware && map > 4)
	{
		// Not possible in the 4-level demo.
		P_SetMessage(&players[consoleplayer], "ACCESS DENIED -- DEMO", true);
		return;
	}

	gameaction = ga_completed;
	LeaveMap = map;
	LeavePosition = position;
}

static void G_DoCompleted(void)
{
	int i;

	gameaction = ga_nothing;
	if (G_CheckDemoStatus())
	{
		return;
	}
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			G_PlayerExitMap(i);
		}
	}
	if (LeaveMap == -1 && LeavePosition == -1)
	{
		gameaction = ga_victory;
		return;
	}
	else
	{
		gamestate = GS_INTERMISSION;
		IN_Start();
	}

	/*
	int i;
	static int afterSecret[3] = { 7, 5, 5 };

	gameaction = ga_nothing;
	if (G_CheckDemoStatus())
	{
		return;
	}
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			G_PlayerFinishLevel(i);
		}
	}
	prevmap = gamemap;
	if (secretexit == true)
	{
		gamemap = 9;
	}
	else if (gamemap == 9)
	{ // Finished secret level
		gamemap = afterSecret[gameepisode - 1];
	}
	else if (gamemap == 8)
	{
		gameaction = ga_victory;
		return;
	}
	else
	{
		gamemap++;
	}
	gamestate = GS_INTERMISSION;
	IN_Start();
	*/
}

//============================================================================
//
// G_WorldDone
//
//============================================================================

void G_WorldDone(void)
{
	gameaction = ga_worlddone;
}

//============================================================================
//
// G_DoWorldDone
//
//============================================================================

static void G_DoWorldDone(void)
{
	gamestate = GS_LEVEL;
	G_DoLoadLevel();
	gameaction = ga_nothing;
	viewactive = true;
}

//==========================================================================
//
// G_DoSingleReborn
//
// Called by G_Ticker based on gameaction.  Loads a game from the reborn
// save slot.
//
//==========================================================================

static void G_DoSingleReborn(void)
{
	gameaction = ga_nothing;
	SV_LoadGame(SV_GetRebornSlot());
	SB_SetClassData();
}

//==========================================================================
//
// G_LoadGame
//
// Can be called by the startup code or the menu task.
//
//==========================================================================

void G_LoadGame(int slot)
{
	loadgameslot = slot;
	gameaction = ga_loadgame;
}

//==========================================================================
//
// G_DoLoadGame
//
// Called by G_Ticker based on gameaction.
//
//==========================================================================

void G_DoLoadGame(void)
{
	gameaction = ga_nothing;
	SV_LoadGame(loadgameslot);
	if (!netgame)
	{ // Copy the base slot to the reborn slot
		SV_UpdateRebornSlot();
	}
	SB_SetClassData();
}

//==========================================================================
//
// G_SaveGame
//
// Called by the menu task.  <description> is a 24 byte text string.
//
//==========================================================================

void G_SaveGame(int slot, const char *description)
{
	savegameslot = slot;
	strcpy(savedescription, description);
	sendsave = true;
}

//==========================================================================
//
// G_DoSaveGame
//
// Called by G_Ticker based on gameaction.
//
//==========================================================================

static void G_DoSaveGame(void)
{
	SV_SaveGame(savegameslot, savedescription);
	gameaction = ga_nothing;
	savedescription[0] = 0;
	P_SetMessage(&players[consoleplayer], TXT_GAMESAVED, true);
}

//==========================================================================
//
// G_DeferredNewGame
//
//==========================================================================

void G_DeferredNewGame(skill_t skill)
{
	TempSkill = skill;
	gameaction = ga_newgame;
}

//==========================================================================
//
// G_DoNewGame
//
//==========================================================================

static void G_DoNewGame(void)
{
	G_StartNewGame(TempSkill);
	gameaction = ga_nothing;
}

/*
====================
=
= G_InitNew
=
= Can be called by the startup code or the menu task
= consoleplayer, displayplayer, playeringame[] should be set
====================
*/

void G_DeferedInitNew(skill_t skill, int episode, int map)
{
	TempSkill = skill;
	TempEpisode = episode;
	TempMap = map;
	gameaction = ga_initnew;
}

static void G_DoInitNew(void)
{
	SV_InitBaseSlot();
	G_InitNew(TempSkill, TempEpisode, TempMap);
	gameaction = ga_nothing;
}

void G_InitNew(skill_t skill, int episode, int map)
{
	int i;

	if (paused)
	{
		paused = false;
		S_ResumeSound();
	}
	if (skill < sk_baby)
	{
		skill = sk_baby;
	}
	if (skill > sk_nightmare)
	{
		skill = sk_nightmare;
	}
	if (map < 1)
	{
		map = 1;
	}
	if (map > 99)
	{
		map = 99;
	}
	M_ClearRandom();
	// Force players to be initialized upon first level load
	for (i = 0; i < MAXPLAYERS; i++)
	{
		players[i].playerstate = PST_REBORN;
		players[i].worldTimer = 0;
	}

	// Set up a bunch of globals
	usergame = true;	// will be set false if a demo
	paused = false;
	demorecording = false;
	demoplayback = false;
	viewactive = true;
	gameepisode = episode;
	gamemap = map;
	gameskill = skill;
	BorderNeedRefresh = true;

	// Initialize the sky
	R_InitSky(map);

	// Give one null ticcmd_t
	//gametic = 0;
	//maketic = 1;
	//for (i = 0; i < MAXPLAYERS; i++)
	//	nettics[i] = 1;	// one null event for this gametic
	//memset (localcmds, 0, sizeof(localcmds));
	//memset (netcmds, 0, sizeof(netcmds));

	G_DoLoadLevel();
}

/*
===============================================================================

							DEMO RECORDING

===============================================================================
*/

#define DEMOMARKER	0x80

static void G_ReadDemoTiccmd (ticcmd_t *cmd)
{
	if (*demo_p == DEMOMARKER)
	{	// end of demo data stream
		G_CheckDemoStatus ();
		return;
	}
	cmd->forwardmove = ((signed char)*demo_p++);
	cmd->sidemove = ((signed char)*demo_p++);
	cmd->angleturn = ((unsigned char)*demo_p++)<<8;
	cmd->buttons = (unsigned char)*demo_p++;
	cmd->lookfly = (unsigned char)*demo_p++;
	cmd->arti = (unsigned char)*demo_p++;
}

static void G_WriteDemoTiccmd (ticcmd_t *cmd)
{
	if (gamekeydown['q'])		// press q to end demo recording
		G_CheckDemoStatus ();
	*demo_p++ = (byte) cmd->forwardmove;
	*demo_p++ = (byte) cmd->sidemove;
	*demo_p++ = cmd->angleturn>>8;
	*demo_p++ = cmd->buttons;
	*demo_p++ = cmd->lookfly;
	*demo_p++ = cmd->arti;
	demo_p -= 6;
	G_ReadDemoTiccmd (cmd);		// make SURE it is exactly the same
}


/*
===================
=
= G_RecordDemo
=
===================
*/

void G_RecordDemo (skill_t skill, int numplayers, int episode, int map, const char *name)
{
	int		i;

	G_InitNew (skill, episode, map);
	usergame = false;
	snprintf (demoname, sizeof(demoname), "%s%s.lmp", basePath, name);
	demobuffer = demo_p = (byte *) Z_Malloc (0x20000, PU_STATIC, NULL);
	*demo_p++ = skill;
	*demo_p++ = episode;
	*demo_p++ = map;

	for (i = 0; i < MAXPLAYERS; i++)
	{
#if (MAXPLAYERS > MAXPLAYERS_10)
	/* 8-player support is added to Hexen starting
	 * with version 1.1.  If using 1.0 wad files,
	 * don't write player data > 4 to the demo...
	 * See also G_DoPlayDemo() below.
	 * FIXME: What if the player count is > 4 ???
	 */
		if (oldwad_10 && i >= MAXPLAYERS_10)
			break;
#endif
		*demo_p++ = playeringame[i];
		*demo_p++ = PlayerClasses[i];
	}
	demorecording = true;
}


/*
===================
=
= G_PlayDemo
=
===================
*/

static const char	*defdemoname;

void G_DeferedPlayDemo (const char *name)
{
	defdemoname = name;
	gameaction = ga_playdemo;
}

static void G_DoPlayDemo (void)
{
	skill_t	skill;
	int	i, episode, map;

	gameaction = ga_nothing;
	demobuffer = demo_p = (byte *) W_CacheLumpName (defdemoname, PU_STATIC);
	skill = *demo_p++;
	episode = *demo_p++;
	map = *demo_p++;

	for (i = 0; i < MAXPLAYERS; i++)
	{
#if (MAXPLAYERS > MAXPLAYERS_10)
	/* here is the tricky part: the demos in the
	 * version 1.0 wad files are for MAXPLAYERS 4,
	 * not 8. the check that I added below is not
	 * accurate, though, because the demo may be
	 * recorded using MAXPLAYERS == 8, ie. it may
	 * not be from the wad file..
	 * See also G_RecordDemo() above.
	 */
		if (oldwad_10 && i >= MAXPLAYERS_10)
		{
			playeringame[i] = 0;
			PlayerClasses[i] = 0;
			continue;
		}
#endif
		playeringame[i] = *demo_p++;
		PlayerClasses[i] = *demo_p++;
	}

	// Initialize world info, etc.
	G_StartNewInit();

	precache = false;		// don't spend a lot of time in loadlevel
	G_InitNew (skill, episode, map);
	precache = true;
	usergame = false;
	demoplayback = true;
}


/*
===================
=
= G_TimeDemo
=
===================
*/

void G_TimeDemo (const char *name)
{
	skill_t	skill;
	int	episode, map;

	demobuffer = demo_p = (byte *) W_CacheLumpName (name, PU_STATIC);
	skill = *demo_p++;
	episode = *demo_p++;
	map = *demo_p++;
	G_InitNew (skill, episode, map);
	usergame = false;
	demoplayback = true;
	timingdemo = true;
	singletics = true;
}


/*
===================
=
= G_CheckDemoStatus
=
= Called after a death or level completion to allow demos to be cleaned up
= Returns true if a new demo loop action will take place
===================
*/

boolean G_CheckDemoStatus (void)
{
	int		endtime;

	if (timingdemo)
	{
		endtime = I_GetTime ();
		I_Error ("timed %i gametics in %i realtics", gametic,
						endtime - starttime);
	}

	if (demoplayback)
	{
		if (singledemo)
			I_Quit ();

		Z_ChangeTag (demobuffer, PU_CACHE);
		demoplayback = false;
		H2_AdvanceDemo();
		return true;
	}

	if (demorecording)
	{
		*demo_p++ = DEMOMARKER;
		M_WriteFile (demoname, demobuffer, demo_p - demobuffer);
		Z_Free (demobuffer);
		demorecording = false;
		I_Error ("Recorded demo: %s", demoname);
	}

	return false;
}

