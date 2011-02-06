/***************************************************************************

    TK-80BS (c) 1980 NEC

    preliminary driver by Angelo Salese

    TODO:
    - (try to) dump proper roms, the whole driver is based off fake roms;
    - Remove the PPI hack;
    - BASIC doesn't seem to work properly;

****************************************************************************/

#include "emu.h"
#include "cpu/i8085/i8085.h"
#include "machine/i8255a.h"


class tk80bs_state : public driver_device
{
public:
	tk80bs_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	UINT8 *vram;
	UINT8 keyb_press;
	UINT8 keyb_press_flag;
	UINT8 shift_press_flag;
};




static VIDEO_START( tk80 )
{
}

static VIDEO_UPDATE( tk80 )
{
    return 0;
}

static ADDRESS_MAP_START(tk80_mem, ADDRESS_SPACE_PROGRAM, 8)
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x0000, 0x02ff) AM_ROM
	AM_RANGE(0x0300, 0x03ff) AM_RAM // EEPROM
	AM_RANGE(0x8000, 0x83ff) AM_RAM // RAM
ADDRESS_MAP_END

static ADDRESS_MAP_START( tk80_io , ADDRESS_SPACE_IO, 8)
	ADDRESS_MAP_UNMAP_HIGH
	//AM_RANGE(0xf8, 0xfb) AM_DEVREADWRITE("ppi8255_0", i8255a_r, i8255a_w)
ADDRESS_MAP_END

/* Input ports */
INPUT_PORTS_START( tk80 )
INPUT_PORTS_END

static MACHINE_RESET(tk80)
{
}


static VIDEO_START( tk80bs )
{
}

static VIDEO_UPDATE( tk80bs )
{
	tk80bs_state *state = screen->machine->driver_data<tk80bs_state>();
	int x,y;
	int count;

	count = 0;

	for(y=0;y<16;y++)
	{
		for(x=0;x<32;x++)
		{
			int tile = state->vram[count];

			drawgfx_opaque(bitmap, cliprect, screen->machine->gfx[0], tile, 0, 0, 0, x*8, y*8);

			count++;
		}
	}

    return 0;
}

/* FIXME: current i8255 core doesn't seem to like this system at all, so we use custom code here for now */
static READ8_HANDLER( ppi_custom_r )
{
	tk80bs_state *state = space->machine->driver_data<tk80bs_state>();
//  printf("%02x\n",offset);

	switch(offset+0x7dfc)
	{
		case 0x7dfc: state->keyb_press_flag = 0; return state->keyb_press;
		case 0x7dfd: return 0xff;
		case 0x7dfe: return state->keyb_press_flag << 5; //keyboard flag
	}

	return 0xff;
}

static WRITE8_HANDLER( ppi_custom_w )
{
	//printf("%02x %02x\n",offset,data);

	switch(offset+0x7dfc)
	{
		case 0x7dff: break; //display mode + ppi8255 mode (0xb2 -> 0x00 -> 0x03)
	}
}

static ADDRESS_MAP_START(tk80bs_mem, ADDRESS_SPACE_PROGRAM, 8)
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x0000, 0x07ff) AM_ROM
//  AM_RANGE(0x0c00, 0x7bff) AM_ROM // ext
	AM_RANGE(0x7df8, 0x7df9) AM_NOP // i8251 sio
//  AM_RANGE(0x7dfc, 0x7dff) AM_DEVREADWRITE("ppi8255_0", i8255a_r, i8255a_w)
	AM_RANGE(0x7dfc, 0x7dff) AM_READWRITE(ppi_custom_r,ppi_custom_w)
	AM_RANGE(0x7e00, 0x7fff) AM_RAM AM_BASE_MEMBER(tk80bs_state, vram) // video ram
	AM_RANGE(0x8000, 0xcfff) AM_RAM // RAM
	AM_RANGE(0xd000, 0xefff) AM_ROM // BASIC
	AM_RANGE(0xf000, 0xffff) AM_ROM // BSMON
ADDRESS_MAP_END


/* Input ports */
static INPUT_PORTS_START( tk80bs )
	PORT_START("key1") //0x00-0x1f
	PORT_BIT(0x00000001,IP_ACTIVE_HIGH,IPT_UNUSED) //0x00 null
	PORT_BIT(0x00000002,IP_ACTIVE_HIGH,IPT_UNUSED) //0x01 soh
	PORT_BIT(0x00000004,IP_ACTIVE_HIGH,IPT_UNUSED) //0x02 stx
	PORT_BIT(0x00000008,IP_ACTIVE_HIGH,IPT_UNUSED) //0x03 etx
	PORT_BIT(0x00000010,IP_ACTIVE_HIGH,IPT_UNUSED) //0x04 etx
	PORT_BIT(0x00000020,IP_ACTIVE_HIGH,IPT_UNUSED) //0x05 eot
	PORT_BIT(0x00000040,IP_ACTIVE_HIGH,IPT_UNUSED) //0x06 enq
	PORT_BIT(0x00000080,IP_ACTIVE_HIGH,IPT_UNUSED) //0x07 ack
	PORT_BIT(0x00000100,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Backspace") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
	PORT_BIT(0x00000200,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Tab") PORT_CODE(KEYCODE_TAB) PORT_CHAR(9)
	PORT_BIT(0x00000400,IP_ACTIVE_HIGH,IPT_UNUSED) //0x0a
	PORT_BIT(0x00000800,IP_ACTIVE_HIGH,IPT_UNUSED) //0x0b lf
	PORT_BIT(0x00001000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x0c vt
	PORT_BIT(0x00002000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("RETURN") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(27)
	PORT_BIT(0x00004000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x0e cr
	PORT_BIT(0x00008000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x0f so

	PORT_BIT(0x00010000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x10 si
	PORT_BIT(0x00020000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x11 dle
	PORT_BIT(0x00040000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x12 dc1
	PORT_BIT(0x00080000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x13 dc2
	PORT_BIT(0x00100000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("HOME") PORT_CODE(KEYCODE_HOME)
	PORT_BIT(0x00200000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x15 dc4
	PORT_BIT(0x00400000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x00800000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x17 syn
	PORT_BIT(0x01000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x18 etb
	PORT_BIT(0x02000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x04000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x1a em
	PORT_BIT(0x08000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("ESC") PORT_CHAR(27)
	PORT_BIT(0x10000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x1c ???
	PORT_BIT(0x20000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x1d fs
	PORT_BIT(0x40000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x1e gs
	PORT_BIT(0x80000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x1f us

	PORT_START("key2") //0x20-0x3f
	PORT_BIT(0x00000001,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Space") PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT(0x00000002,IP_ACTIVE_HIGH,IPT_UNUSED) //0x21 !
	PORT_BIT(0x00000004,IP_ACTIVE_HIGH,IPT_UNUSED) //0x22 "
	PORT_BIT(0x00000008,IP_ACTIVE_HIGH,IPT_UNUSED) //0x23 #
	PORT_BIT(0x00000010,IP_ACTIVE_HIGH,IPT_UNUSED) //0x24 $
	PORT_BIT(0x00000020,IP_ACTIVE_HIGH,IPT_UNUSED) //0x25 %
	PORT_BIT(0x00000040,IP_ACTIVE_HIGH,IPT_UNUSED) //0x26 &
	PORT_BIT(0x00000080,IP_ACTIVE_HIGH,IPT_UNUSED) //0x27 '
	PORT_BIT(0x00000100,IP_ACTIVE_HIGH,IPT_UNUSED) //0x28 (
	PORT_BIT(0x00000200,IP_ACTIVE_HIGH,IPT_UNUSED) //0x29 )
	PORT_BIT(0x00000400,IP_ACTIVE_HIGH,IPT_UNUSED) //0x2a *
	PORT_BIT(0x00000800,IP_ACTIVE_HIGH,IPT_UNUSED)
	PORT_BIT(0x00001000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x2c ,
	PORT_BIT(0x00002000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("-") PORT_CODE(KEYCODE_MINUS) PORT_CHAR('-')
	PORT_BIT(0x00004000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x2e .
	PORT_BIT(0x00008000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x2f /

	PORT_BIT(0x00010000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("0") PORT_CODE(KEYCODE_0) PORT_CHAR('0')
	PORT_BIT(0x00020000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("1") PORT_CODE(KEYCODE_1) PORT_CHAR('1')
	PORT_BIT(0x00040000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("2") PORT_CODE(KEYCODE_2) PORT_CHAR('2')
	PORT_BIT(0x00080000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("3") PORT_CODE(KEYCODE_3) PORT_CHAR('3')
	PORT_BIT(0x00100000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("4") PORT_CODE(KEYCODE_4) PORT_CHAR('4')
	PORT_BIT(0x00200000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("5") PORT_CODE(KEYCODE_5) PORT_CHAR('5')
	PORT_BIT(0x00400000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("6") PORT_CODE(KEYCODE_6) PORT_CHAR('6')
	PORT_BIT(0x00800000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("7") PORT_CODE(KEYCODE_7) PORT_CHAR('7')
	PORT_BIT(0x01000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("8") PORT_CODE(KEYCODE_8) PORT_CHAR('8')
	PORT_BIT(0x02000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("9") PORT_CODE(KEYCODE_9) PORT_CHAR('9')
	PORT_BIT(0x04000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME(":") PORT_CODE(KEYCODE_QUOTE) PORT_CHAR(':')
	PORT_BIT(0x08000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME(";") PORT_CODE(KEYCODE_COLON) PORT_CHAR(';')
	PORT_BIT(0x10000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("<") PORT_CODE(KEYCODE_BACKSLASH2) PORT_CHAR('<')
	PORT_BIT(0x20000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x3d =
	PORT_BIT(0x40000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x3e >
	PORT_BIT(0x80000000,IP_ACTIVE_HIGH,IPT_UNUSED) //0x3f ?

	PORT_START("key3") //0x40-0x5f
	PORT_BIT(0x00000001,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("@") PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('@')
	PORT_BIT(0x00000002,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("A") PORT_CODE(KEYCODE_A) PORT_CHAR('A')
	PORT_BIT(0x00000004,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("B") PORT_CODE(KEYCODE_B) PORT_CHAR('B')
	PORT_BIT(0x00000008,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("C") PORT_CODE(KEYCODE_C) PORT_CHAR('C')
	PORT_BIT(0x00000010,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("D") PORT_CODE(KEYCODE_D) PORT_CHAR('D')
	PORT_BIT(0x00000020,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("E") PORT_CODE(KEYCODE_E) PORT_CHAR('E')
	PORT_BIT(0x00000040,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("F") PORT_CODE(KEYCODE_F) PORT_CHAR('F')
	PORT_BIT(0x00000080,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("G") PORT_CODE(KEYCODE_G) PORT_CHAR('G')
	PORT_BIT(0x00000100,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("H") PORT_CODE(KEYCODE_H) PORT_CHAR('H')
	PORT_BIT(0x00000200,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("I") PORT_CODE(KEYCODE_I) PORT_CHAR('I')
	PORT_BIT(0x00000400,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("J") PORT_CODE(KEYCODE_J) PORT_CHAR('J')
	PORT_BIT(0x00000800,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("K") PORT_CODE(KEYCODE_K) PORT_CHAR('K')
	PORT_BIT(0x00001000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("L") PORT_CODE(KEYCODE_L) PORT_CHAR('L')
	PORT_BIT(0x00002000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("M") PORT_CODE(KEYCODE_M) PORT_CHAR('M')
	PORT_BIT(0x00004000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("N") PORT_CODE(KEYCODE_N) PORT_CHAR('N')
	PORT_BIT(0x00008000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("O") PORT_CODE(KEYCODE_O) PORT_CHAR('O')
	PORT_BIT(0x00010000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("P") PORT_CODE(KEYCODE_P) PORT_CHAR('P')
	PORT_BIT(0x00020000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Q") PORT_CODE(KEYCODE_Q) PORT_CHAR('Q')
	PORT_BIT(0x00040000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("R") PORT_CODE(KEYCODE_R) PORT_CHAR('R')
	PORT_BIT(0x00080000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("S") PORT_CODE(KEYCODE_S) PORT_CHAR('S')
	PORT_BIT(0x00100000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("T") PORT_CODE(KEYCODE_T) PORT_CHAR('T')
	PORT_BIT(0x00200000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("U") PORT_CODE(KEYCODE_U) PORT_CHAR('U')
	PORT_BIT(0x00400000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("V") PORT_CODE(KEYCODE_V) PORT_CHAR('V')
	PORT_BIT(0x00800000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("W") PORT_CODE(KEYCODE_W) PORT_CHAR('W')
	PORT_BIT(0x01000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("X") PORT_CODE(KEYCODE_X) PORT_CHAR('X')
	PORT_BIT(0x02000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Y") PORT_CODE(KEYCODE_Y) PORT_CHAR('Y')
	PORT_BIT(0x04000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Z") PORT_CODE(KEYCODE_Z) PORT_CHAR('Z')
	PORT_BIT(0x08000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("[") PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('[')
	PORT_BIT(0x10000000,IP_ACTIVE_HIGH,IPT_UNUSED)
	PORT_BIT(0x20000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("]") PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR(']')
	PORT_BIT(0x40000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("^") PORT_CODE(KEYCODE_EQUALS) PORT_CHAR('^')
	PORT_BIT(0x80000000,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("_")

	PORT_START("key_modifiers")
	PORT_BIT(0x00000001,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("CTRL") PORT_CODE(KEYCODE_LCONTROL)
	PORT_BIT(0x00000002,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT(0x00000004,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("KANA") PORT_CODE(KEYCODE_RCONTROL)
	PORT_BIT(0x00000008,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("CAPS") PORT_CODE(KEYCODE_CAPSLOCK) PORT_TOGGLE
	PORT_BIT(0x00000010,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("GRPH") PORT_CODE(KEYCODE_LALT)
INPUT_PORTS_END

static TIMER_CALLBACK( keyboard_callback )
{
	tk80bs_state *state = machine->driver_data<tk80bs_state>();
	static const char *const portnames[3] = { "key1","key2","key3" };
	int i,port_i,scancode;
	UINT8 keymod = input_port_read(machine,"key_modifiers") & 0x1f;
	scancode = 0;

	state->shift_press_flag = ((keymod & 0x02) >> 1);

	for(port_i=0;port_i<3;port_i++)
	{
		for(i=0;i<32;i++)
		{
			if((input_port_read(machine,portnames[port_i])>>i) & 1)
			{
				if(keymod & 0x04) // kana key pressed
				{
					scancode |= 0x80;
				}
				if(!state->shift_press_flag)  // shift not pressed
				{
					//if(scancode >= 0x41 && scancode < 0x5b)
					//  scancode += 0x20;  // lowercase doesn't exist here
				}
				else
				{
					if(scancode >= 0x31 && scancode < 0x3a)
						scancode -= 0x10;
					if(scancode == 0x30)
						scancode = 0x3d;

					if(scancode == 0x3b)
						scancode = 0x2c;

					if(scancode == 0x3a)
						scancode = 0x2e;
					if(scancode == 0x5b)
						scancode = 0x2b;
					if(scancode == 0x3c)
						scancode = 0x3e;
				}
				state->keyb_press = scancode;
				state->keyb_press_flag = 1;
				return;
			}
			scancode++;
		}
	}
}

static MACHINE_START(tk80bs)
{
	machine->scheduler().timer_pulse(attotime::from_hz(240/32), FUNC(keyboard_callback));
}

static MACHINE_RESET(tk80bs)
{
}

/* F4 Character Displayer */
static const gfx_layout tk80bs_charlayout =
{
	8, 8,
	512,
	1,
	{ 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8
};

static GFXDECODE_START( tk80bs )
	GFXDECODE_ENTRY( "gfx", 0x0000, tk80bs_charlayout, 0, 1 )
GFXDECODE_END

static READ8_DEVICE_HANDLER( key_matrix_r )
{
	printf("A R\n");

	return 0;
}

static READ8_DEVICE_HANDLER( serial_in_r )
{
	printf("B R\n");

	return 0;
}

static WRITE8_DEVICE_HANDLER( serial_out_w )
{
}

static I8255A_INTERFACE( ppi8255_intf_0 )
{
	DEVCB_HANDLER(key_matrix_r),		/* Port A read */
	DEVCB_HANDLER(serial_in_r),			/* Port B read */
	DEVCB_NULL,							/* Port C read */
	DEVCB_NULL,							/* Port A write */
	DEVCB_NULL,							/* Port B write */
	DEVCB_HANDLER(serial_out_w)			/* Port C write */
};


static MACHINE_CONFIG_START( tk80, tk80bs_state )
    /* basic machine hardware */
    MCFG_CPU_ADD("maincpu",I8080, XTAL_1MHz)
    MCFG_CPU_PROGRAM_MAP(tk80_mem)
    MCFG_CPU_IO_MAP(tk80_io)

    MCFG_MACHINE_RESET(tk80)

    /* video hardware */
    MCFG_SCREEN_ADD("screen", RASTER)
    MCFG_SCREEN_REFRESH_RATE(50)
    MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
    MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
    MCFG_SCREEN_SIZE(640, 480)
    MCFG_SCREEN_VISIBLE_AREA(0, 640-1, 0, 480-1)
    MCFG_PALETTE_LENGTH(2)
    MCFG_PALETTE_INIT(black_and_white)

	MCFG_I8255A_ADD( "ppi8255_0", ppi8255_intf_0 )
	
    MCFG_VIDEO_START(tk80)
    MCFG_VIDEO_UPDATE(tk80)
MACHINE_CONFIG_END

static MACHINE_CONFIG_START( tk80bs, tk80bs_state )
    /* basic machine hardware */
    MCFG_CPU_ADD("maincpu",I8080, XTAL_1MHz) //unknown clock
    MCFG_CPU_PROGRAM_MAP(tk80bs_mem)

//  MCFG_I8255A_ADD( "ppi8255_0", ppi8255_intf_0 )

    MCFG_MACHINE_START(tk80bs)
    MCFG_MACHINE_RESET(tk80bs)

    /* video hardware */
    MCFG_SCREEN_ADD("screen", RASTER)
    MCFG_SCREEN_REFRESH_RATE(50)
    MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
    MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
    MCFG_SCREEN_SIZE(256, 128)
    MCFG_SCREEN_VISIBLE_AREA(0, 256-1, 0, 128-1)
    MCFG_PALETTE_LENGTH(2)
    MCFG_PALETTE_INIT(black_and_white)
	MCFG_GFXDECODE(tk80bs)

    MCFG_VIDEO_START(tk80bs)
    MCFG_VIDEO_UPDATE(tk80bs)
MACHINE_CONFIG_END

/* ROM definition */
ROM_START( tk80 )
    ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASEFF )
	ROM_LOAD( "tk80-1.bin", 0x0000, 0x0100, CRC(897295e4) SHA1(50fb42b07252fc48044830e2f228e218fc59481c))
	ROM_LOAD( "tk80-2.bin", 0x0100, 0x0100, CRC(d54480c3) SHA1(354962aca1710ac75b40c8c23a6c303938f9d596))
	ROM_LOAD( "tk80-3.bin", 0x0200, 0x0100, CRC(8d4b02ef) SHA1(2b5a1ee8f97db23ffec48b96f12986461024c995))
ROM_END

ROM_START( tk80bs )
    ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASEFF )
    /* all of these aren't taken from an original machine*/
	ROM_SYSTEM_BIOS(0, "psedo", "Pseudo LEVEL 1")
	ROMX_LOAD( "tk80.dummy", 0x0000, 0x0800, BAD_DUMP CRC(553b25ca) SHA1(939350d7fa56ce567ddf393c9f4b9db6ebc18a2c), ROM_BIOS(1))
	ROMX_LOAD( "ext.l1",     0x0c00, 0x6e46, BAD_DUMP CRC(d05ed3ff) SHA1(8544aa2cb58df9edf221f5be2cdafa248dd33828), ROM_BIOS(1))
	ROMX_LOAD( "lv1basic.l1",0xe000, 0x09a2, BAD_DUMP CRC(3ff67a71) SHA1(528c9331740637e853c099e1739ecdea6dd200bc), ROM_BIOS(1))
	ROMX_LOAD( "bsmon.l1",   0xf000, 0x0db0, BAD_DUMP CRC(5daa599b) SHA1(7e6ec5bfb3eea114f7ee9ef589a89246b8533b2f), ROM_BIOS(1))

	ROM_SYSTEM_BIOS(1, "psedo10", "Pseudo LEVEL 2 1.0")
	ROMX_LOAD( "tk80.dummy", 0x0000, 0x0800, BAD_DUMP CRC(553b25ca) SHA1(939350d7fa56ce567ddf393c9f4b9db6ebc18a2c), ROM_BIOS(2))
	ROMX_LOAD( "ext.10",     0x0c00, 0x3dc2, BAD_DUMP CRC(3c64d488) SHA1(919180d5b34b981ab3dd8b2885d3c0933203f355), ROM_BIOS(2))
	ROMX_LOAD( "lv2basic.10",0xd000, 0x2000, BAD_DUMP CRC(594fe70e) SHA1(5854c1be5fa78c1bfee365379495f14bc23e15e7), ROM_BIOS(2))
	ROMX_LOAD( "bsmon.10",   0xf000, 0x0daf, BAD_DUMP CRC(d0047983) SHA1(79e2b5dc47b574b55375cbafffff144744093ec1), ROM_BIOS(2))

	ROM_SYSTEM_BIOS(2, "psedo11", "Pseudo LEVEL 2 1.1")
	ROMX_LOAD( "tk80.dummy", 0x0000, 0x0800, BAD_DUMP CRC(553b25ca) SHA1(939350d7fa56ce567ddf393c9f4b9db6ebc18a2c), ROM_BIOS(3))
	ROMX_LOAD( "ext.11",     0x0c00, 0x3dd4, BAD_DUMP CRC(bd5c5169) SHA1(2ad70828348372328b76bac0fa93d3f6f17ade34), ROM_BIOS(3))
	ROMX_LOAD( "lv2basic.11",0xd000, 0x2000, BAD_DUMP CRC(3df9a3bd) SHA1(9539409c876bce27d630fe47d07a4316d2ce09cb), ROM_BIOS(3))
	ROMX_LOAD( "bsmon.11",   0xf000, 0x0ff6, BAD_DUMP CRC(fca7a609) SHA1(7c7eb5e5e4cf1e0021383bdfc192b88262aba6f5), ROM_BIOS(3))

	ROM_REGION( 0x1000, "gfx", ROMREGION_ERASEFF )
	ROM_LOAD( "font.rom", 0x0000, 0x1000, BAD_DUMP CRC(94d95199) SHA1(9fe741eab866b0c520d4108bccc6277172fa190c))
ROM_END

/* Driver */

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    INIT     COMPANY   FULLNAME       FLAGS */
COMP( 1976, tk80,	0,      0,       tk80,      tk80,    0,       "Nippon Electronic Company",   "TK-80",		GAME_NOT_WORKING | GAME_NO_SOUND)
COMP( 1980, tk80bs, tk80,   0,       tk80bs,    tk80bs,  0,       "Nippon Electronic Company",   "TK-80BS",		GAME_NOT_WORKING | GAME_NO_SOUND)

