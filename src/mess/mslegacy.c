/*********************************************************************

    mslegacy.c

    Defines since removed from MAME, but kept in MESS for legacy
    reasons

*********************************************************************/

#include "emu.h"
#include "mslegacy.h"


static const char *const mess_default_text[] =
{
	"OK",
	"Off",
	"On",
	"Dip Switches",
	"Driver Configuration",
	"Analog Controls",
	"Digital Speed",
	"Autocenter Speed",
	"Reverse",
	"Sensitivity",

	"Keyboard Emulation Status",
	"-------------------------",
	"Mode: PARTIAL Emulation",
	"Mode: FULL Emulation",
	"UI:   Enabled",
	"UI:   Disabled",
	"**Use ScrLock to toggle**",

	"No Tape Image loaded",
	"recording",
	"playing",
	"(recording)",
	"(playing)",
	"stopped",
	"Pause/Stop",
	"Record",
	"Play",
	"Rewind",
	"Fast Forward",
	"Mount...",
	"Create...",
	"Unmount",
	"[empty slot]",
	"Input Devices",
	"Quit Fileselector",
	"File Specification"
};



/***************************************************************************
    UI TEXT
***************************************************************************/

const char * ui_getstring (int string_num)
{
	return mess_default_text[string_num];
}
