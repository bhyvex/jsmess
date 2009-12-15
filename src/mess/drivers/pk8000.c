/***************************************************************************

        PK-8000

        18/07/2009 Mostly working driver
        12/05/2009 Skeleton driver.

****************************************************************************/

#include "driver.h"
#include "cpu/i8085/i8085.h"
#include "machine/i8255a.h"
#include "devices/cassette.h"
#include "formats/fmsx_cas.h"
#include "sound/speaker.h"
#include "sound/wave.h"
#include "includes/pk8000.h"
#include "devices/messram.h"

static UINT8 keyboard_line;

static const device_config *cassette_device_image(running_machine *machine)
{
	return devtag_get_device(machine, "cassette");
}

static void pk8000_set_bank(running_machine *machine,UINT8 data)
{
	UINT8 *rom = memory_region(machine, "maincpu");
	UINT8 block1 = data & 3;
	UINT8 block2 = (data >> 2) & 3;
	UINT8 block3 = (data >> 4) & 3;
	UINT8 block4 = (data >> 6) & 3;

	switch(block1) {
		case 0:
				memory_set_bankptr(machine, "bank1", rom + 0x10000);
				memory_set_bankptr(machine, "bank5", messram_get_ptr(devtag_get_device(machine, "messram")));
				break;
		case 1: break;
		case 2: break;
		case 3:
				memory_set_bankptr(machine, "bank1", messram_get_ptr(devtag_get_device(machine, "messram")));
				memory_set_bankptr(machine, "bank5", messram_get_ptr(devtag_get_device(machine, "messram")));
				break;
	}

	switch(block2) {
		case 0:
				memory_set_bankptr(machine, "bank2", rom + 0x14000);
				memory_set_bankptr(machine, "bank6", messram_get_ptr(devtag_get_device(machine, "messram")) + 0x4000);
				break;
		case 1: break;
		case 2: break;
		case 3:
				memory_set_bankptr(machine, "bank2", messram_get_ptr(devtag_get_device(machine, "messram")) + 0x4000);
				memory_set_bankptr(machine, "bank6", messram_get_ptr(devtag_get_device(machine, "messram")) + 0x4000);
				break;
	}
	switch(block3) {
		case 0:
				memory_set_bankptr(machine, "bank3", rom + 0x18000);
				memory_set_bankptr(machine, "bank7", messram_get_ptr(devtag_get_device(machine, "messram")) + 0x8000);
				break;
		case 1: break;
		case 2: break;
		case 3:
				memory_set_bankptr(machine, "bank3", messram_get_ptr(devtag_get_device(machine, "messram")) + 0x8000);
				memory_set_bankptr(machine, "bank7", messram_get_ptr(devtag_get_device(machine, "messram")) + 0x8000);
				break;
	}
	switch(block4) {
		case 0:
				memory_set_bankptr(machine, "bank4", rom + 0x1c000);
				memory_set_bankptr(machine, "bank8", messram_get_ptr(devtag_get_device(machine, "messram")) + 0xc000);
				break;
		case 1: break;
		case 2: break;
		case 3:
				memory_set_bankptr(machine, "bank4", messram_get_ptr(devtag_get_device(machine, "messram")) + 0xc000);
				memory_set_bankptr(machine, "bank8", messram_get_ptr(devtag_get_device(machine, "messram")) + 0xc000);
				break;
	}
}
static WRITE8_DEVICE_HANDLER(pk8000_80_porta_w)
{
	pk8000_set_bank(device->machine,data);
}

static READ8_DEVICE_HANDLER(pk8000_80_portb_r)
{
	static const char *const keynames[] = { "LINE0", "LINE1", "LINE2", "LINE3", "LINE4", "LINE5", "LINE6", "LINE7", "LINE8", "LINE9" };
	if(keyboard_line>9) {
		return 0xff;
	}
	return input_port_read(device->machine,keynames[keyboard_line]);
}

static WRITE8_DEVICE_HANDLER(pk8000_80_portc_w)
{
	keyboard_line = data & 0x0f;

	speaker_level_w(devtag_get_device(device->machine, "speaker"), BIT(data,7));

	cassette_change_state(cassette_device_image(device->machine),
						(BIT(data,4)) ? CASSETTE_MOTOR_ENABLED : CASSETTE_MOTOR_DISABLED,
						CASSETTE_MASK_MOTOR);
	cassette_output(cassette_device_image(device->machine), (BIT(data,6)) ? +1.0 : 0.0);
}

static I8255A_INTERFACE( pk8000_ppi8255_interface_1 )
{
	DEVCB_NULL,
	DEVCB_HANDLER(pk8000_80_portb_r),
	DEVCB_NULL,
	DEVCB_HANDLER(pk8000_80_porta_w),
	DEVCB_NULL,
	DEVCB_HANDLER(pk8000_80_portc_w)
};

static READ8_DEVICE_HANDLER(pk8000_84_porta_r)
{
	return pk8000_video_mode;
}

static WRITE8_DEVICE_HANDLER(pk8000_84_porta_w)
{
	pk8000_video_mode = data;
}

static WRITE8_DEVICE_HANDLER(pk8000_84_portc_w)
{
	pk8000_video_enable = BIT(data,4);
}
static I8255A_INTERFACE( pk8000_ppi8255_interface_2 )
{
	DEVCB_HANDLER(pk8000_84_porta_r),
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_HANDLER(pk8000_84_porta_w),
	DEVCB_NULL,
	DEVCB_HANDLER(pk8000_84_portc_w)
};

static READ8_HANDLER(pk8000_joy_1_r)
{
	UINT8 retVal = (cassette_input(cassette_device_image(space->machine)) > 0.0038 ? 0x80 : 0);
	retVal |= input_port_read(space->machine, "JOY1") & 0x7f;
	return retVal;
}
static READ8_HANDLER(pk8000_joy_2_r)
{
	UINT8 retVal = (cassette_input(cassette_device_image(space->machine)) > 0.0038 ? 0x80 : 0);
	retVal |= input_port_read(space->machine, "JOY2") & 0x7f;
	return retVal;
}

static ADDRESS_MAP_START(pk8000_mem, ADDRESS_SPACE_PROGRAM, 8)
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE( 0x0000, 0x3fff ) AM_READ_BANK("bank1") AM_WRITE_BANK("bank5")
	AM_RANGE( 0x4000, 0x7fff ) AM_READ_BANK("bank2") AM_WRITE_BANK("bank6")
	AM_RANGE( 0x8000, 0xbfff ) AM_READ_BANK("bank3") AM_WRITE_BANK("bank7")
	AM_RANGE( 0xc000, 0xffff ) AM_READ_BANK("bank4") AM_WRITE_BANK("bank8")
ADDRESS_MAP_END

static ADDRESS_MAP_START( pk8000_io , ADDRESS_SPACE_IO, 8)
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x80, 0x83) AM_DEVREADWRITE("ppi8255_1", i8255a_r, i8255a_w)
	AM_RANGE(0x84, 0x87) AM_DEVREADWRITE("ppi8255_2", i8255a_r, i8255a_w)
	AM_RANGE(0x88, 0x88) AM_READWRITE(pk8000_video_color_r,pk8000_video_color_w)
	AM_RANGE(0x8c, 0x8c) AM_READ(pk8000_joy_1_r)
	AM_RANGE(0x8d, 0x8d) AM_READ(pk8000_joy_2_r)
	AM_RANGE(0x90, 0x90) AM_READWRITE(pk8000_text_start_r,pk8000_text_start_w)
	AM_RANGE(0x91, 0x91) AM_READWRITE(pk8000_chargen_start_r,pk8000_chargen_start_w)
	AM_RANGE(0x92, 0x92) AM_READWRITE(pk8000_video_start_r,pk8000_video_start_w)
	AM_RANGE(0x93, 0x93) AM_READWRITE(pk8000_color_start_r,pk8000_color_start_w)
	AM_RANGE(0xa0, 0xbf) AM_READWRITE(pk8000_color_r,pk8000_color_w)
ADDRESS_MAP_END

/*   Input ports */
static INPUT_PORTS_START( pk8000 )
	PORT_START("LINE0")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0") PORT_CODE(KEYCODE_0)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1") PORT_CODE(KEYCODE_1)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2") PORT_CODE(KEYCODE_2)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3") PORT_CODE(KEYCODE_3)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4") PORT_CODE(KEYCODE_4)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5") PORT_CODE(KEYCODE_5)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6") PORT_CODE(KEYCODE_6)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7") PORT_CODE(KEYCODE_7)
	PORT_START("LINE1")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8") PORT_CODE(KEYCODE_8)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9") PORT_CODE(KEYCODE_9)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(",") PORT_CODE(KEYCODE_COMMA)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("-") PORT_CODE(KEYCODE_MINUS)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".") PORT_CODE(KEYCODE_STOP)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(":") PORT_CODE(KEYCODE_QUOTE)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(";") PORT_CODE(KEYCODE_COLON)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("?") PORT_CODE(KEYCODE_SLASH)
	PORT_START("LINE2")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("[") PORT_CODE(KEYCODE_OPENBRACE)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("|") PORT_CODE(KEYCODE_BACKSLASH)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("]") PORT_CODE(KEYCODE_CLOSEBRACE)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("~") PORT_CODE(KEYCODE_TILDE)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("^") PORT_CODE(KEYCODE_EQUALS)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("@") PORT_CODE(KEYCODE_MINUS_PAD)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A") PORT_CODE(KEYCODE_A)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B") PORT_CODE(KEYCODE_B)
	PORT_START("LINE3")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C") PORT_CODE(KEYCODE_C)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D") PORT_CODE(KEYCODE_D)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E") PORT_CODE(KEYCODE_E)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F") PORT_CODE(KEYCODE_F)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G") PORT_CODE(KEYCODE_G)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H") PORT_CODE(KEYCODE_H)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("I") PORT_CODE(KEYCODE_I)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("J") PORT_CODE(KEYCODE_J)
	PORT_START("LINE4")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("K") PORT_CODE(KEYCODE_K)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L") PORT_CODE(KEYCODE_L)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("M") PORT_CODE(KEYCODE_M)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("N") PORT_CODE(KEYCODE_N)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("O") PORT_CODE(KEYCODE_O)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("P") PORT_CODE(KEYCODE_P)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Q") PORT_CODE(KEYCODE_Q)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R") PORT_CODE(KEYCODE_R)
	PORT_START("LINE5")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("S") PORT_CODE(KEYCODE_S)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("T") PORT_CODE(KEYCODE_T)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("U") PORT_CODE(KEYCODE_U)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("V") PORT_CODE(KEYCODE_V)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("W") PORT_CODE(KEYCODE_W)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("X") PORT_CODE(KEYCODE_X)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Y") PORT_CODE(KEYCODE_Y)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Z") PORT_CODE(KEYCODE_Z)
	PORT_START("LINE6")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Rg") PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Upr") PORT_CODE(KEYCODE_LCONTROL)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Graf") PORT_CODE(KEYCODE_F10)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Alf") PORT_CODE(KEYCODE_F9)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("???") PORT_CODE(KEYCODE_LALT)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F1") PORT_CODE(KEYCODE_F1)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F2") PORT_CODE(KEYCODE_F2)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F3") PORT_CODE(KEYCODE_F3)
	PORT_START("LINE7")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F4") PORT_CODE(KEYCODE_F4)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F5") PORT_CODE(KEYCODE_F5)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Esc") PORT_CODE(KEYCODE_ESC)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Tab") PORT_CODE(KEYCODE_TAB)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Stop") PORT_CODE(KEYCODE_F12)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("VSh") PORT_CODE(KEYCODE_BACKSPACE)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Sel") PORT_CODE(KEYCODE_RCONTROL)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER)
	PORT_START("LINE8")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Probel") PORT_CODE(KEYCODE_SPACE)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Cls") PORT_CODE(KEYCODE_HOME)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Ins") PORT_CODE(KEYCODE_INSERT)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Del") PORT_CODE(KEYCODE_DEL)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Up") PORT_CODE(KEYCODE_UP)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_START("LINE9")
		PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Up-Left") PORT_CODE(KEYCODE_7_PAD)
		PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Down-Right") PORT_CODE(KEYCODE_3_PAD)
		PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Menu") PORT_CODE(KEYCODE_5_PAD)
		PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Begin") PORT_CODE(KEYCODE_4_PAD)
		PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("End") PORT_CODE(KEYCODE_6_PAD)
		PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("End page") PORT_CODE(KEYCODE_PGDN)
		PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Begin page") PORT_CODE(KEYCODE_PGUP)
		PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_START("JOY1")
		PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP) PORT_CATEGORY(10) PORT_PLAYER(1)
		PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN) PORT_CATEGORY(10) PORT_PLAYER(1)
		PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT) PORT_CATEGORY(10) PORT_PLAYER(1)
		PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT) PORT_CATEGORY(10) PORT_PLAYER(1)
		PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_CATEGORY(10) PORT_PLAYER(1)
		PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_CATEGORY(10) PORT_PLAYER(1)
		PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)
		PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_START("JOY2")
		PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP) PORT_CATEGORY(10) PORT_PLAYER(2)
		PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN) PORT_CATEGORY(10) PORT_PLAYER(2)
		PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT) PORT_CATEGORY(10) PORT_PLAYER(2)
		PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT) PORT_CATEGORY(10) PORT_PLAYER(2)
		PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_CATEGORY(10) PORT_PLAYER(2)
		PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_CATEGORY(10) PORT_PLAYER(2)
		PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)
		PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END

static INTERRUPT_GEN( pk8000_interrupt )
{
	cpu_set_input_line(device, 0, HOLD_LINE);
}

static IRQ_CALLBACK(pk8000_irq_callback)
{
	return 0xff;
}


static MACHINE_RESET(pk8000)
{
	pk8000_set_bank(machine,0);
	cpu_set_irq_callback(cputag_get_cpu(machine, "maincpu"), pk8000_irq_callback);
}

static VIDEO_START( pk8000 )
{
}

static VIDEO_UPDATE( pk8000 )
{
	return pk8000_video_update(screen, bitmap, cliprect, messram_get_ptr(devtag_get_device(screen->machine, "messram")));
}

/* Machine driver */
static const cassette_config pk8000_cassette_config =
{
	fmsx_cassette_formats,
	NULL,
	CASSETTE_PLAY
};

static MACHINE_DRIVER_START( pk8000 )
    /* basic machine hardware */
    MDRV_CPU_ADD("maincpu",8080, 1780000)
    MDRV_CPU_PROGRAM_MAP(pk8000_mem)
    MDRV_CPU_IO_MAP(pk8000_io)
    MDRV_CPU_VBLANK_INT("screen", pk8000_interrupt)

    MDRV_MACHINE_RESET(pk8000)

    /* video hardware */
    MDRV_SCREEN_ADD("screen", RASTER)
    MDRV_SCREEN_REFRESH_RATE(50)
    MDRV_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
    MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
    MDRV_SCREEN_SIZE(256+32, 192+32)
    MDRV_SCREEN_VISIBLE_AREA(0, 256+32-1, 0, 192+32-1)
    MDRV_PALETTE_LENGTH(16)
    MDRV_PALETTE_INIT(pk8000)

    MDRV_VIDEO_START(pk8000)
    MDRV_VIDEO_UPDATE(pk8000)

    MDRV_I8255A_ADD( "ppi8255_1", pk8000_ppi8255_interface_1 )
    MDRV_I8255A_ADD( "ppi8255_2", pk8000_ppi8255_interface_2 )

    /* audio hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD("speaker", SPEAKER, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)
	MDRV_SOUND_WAVE_ADD("wave", "cassette")
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

    MDRV_CASSETTE_ADD( "cassette", pk8000_cassette_config )
	
	/* internal ram */
	MDRV_RAM_ADD("messram")
	MDRV_RAM_DEFAULT_SIZE("64K")	
MACHINE_DRIVER_END

/* ROM definition */
ROM_START( vesta )
  ROM_REGION( 0x18000, "maincpu", ROMREGION_ERASEFF )
  ROM_LOAD( "vesta.rom", 0x10000, 0x4000, CRC(fbf7e2cc) SHA1(4bc5873066124bd926c3c6aa2fd1a062c87af339))
ROM_END

ROM_START( hobby )
  ROM_REGION( 0x18000, "maincpu", ROMREGION_ERASEFF )
  ROM_LOAD( "hobby.rom", 0x10000, 0x4000, CRC(a25b4b2c) SHA1(0d86e6e4be8d1aa389bfa9dd79e3604a356729f7))
ROM_END

/* Driver */

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    INIT    CONFIG COMPANY   FULLNAME       FLAGS */
COMP( 1987, vesta,  0,       0, 	pk8000, 	pk8000, 	 0,  	  0,  	 "BP EVM",   "PK8000 Vesta",		0)
COMP( 1987, hobby,  vesta,   0, 	pk8000, 	pk8000, 	 0,  	  0,  	 "BP EVM",   "PK8000 Sura/Hobby",	0)

