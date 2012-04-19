/***************************************************************************

    JPM System 5
    and
    JPM System 5 with Video Expansion 2 hardware

    driver by Phil Bennett

    Video System Games supported:
        * Monopoly
        * Monopoly Classic
        * Monopoly Deluxe

    Known Issues:
        * Artwork support is needed as the monitor bezel illuminates
        to indicate progress through the games.
        * Features used by the AWP games such as lamps, reels and
        meters are not emulated.

    AWP game notes:
      The byte at 0x81 of the EVEN 68k rom appears to be some kind of
      control byte, probably region, or coin / machine type setting.
      Many sets differ only by this byte.

      Many sets are probably missing sound roms, however due to the
      varying motherboard configurations (SAA vs. YM, with added UPD)
      it's hard to tell until they start doing more.

***************************************************************************/

#include "emu.h"
#include "cpu/m68000/m68000.h"
#include "machine/6840ptm.h"
#include "machine/6850acia.h"
#include "sound/2413intf.h"
#include "sound/upd7759.h"
#include "video/tms34061.h"
#include "machine/nvram.h"
#include "video/awpvid.h"
#include "machine/steppers.h"
#include "machine/roc10937.h"


enum state { IDLE, START, DATA, STOP1, STOP2 };

class jpmsys5_state : public driver_device
{
public:
	jpmsys5_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag) { }

	UINT8 m_palette[16][3];
	int m_pal_addr;
	int m_pal_idx;
	int m_touch_state;
	emu_timer *m_touch_timer;
	int m_touch_data_count;
	int m_touch_data[3];
	int m_touch_shift_cnt;
	int m_lamp_strobe;
	int m_mpxclk;
	int m_muxram[255];
	UINT8 m_a0_acia_dcd;
	UINT8 m_a0_data_out;
	UINT8 m_a0_data_in;
	UINT8 m_a1_acia_dcd;
	UINT8 m_a1_data_out;
	UINT8 m_a1_data_in;
	UINT8 m_a2_acia_dcd;
	UINT8 m_a2_data_out;
	UINT8 m_a2_data_in;
	DECLARE_WRITE16_MEMBER(sys5_tms34061_w);
	DECLARE_READ16_MEMBER(sys5_tms34061_r);
	DECLARE_WRITE16_MEMBER(ramdac_w);
	DECLARE_WRITE16_MEMBER(rombank_w);
	DECLARE_READ16_MEMBER(coins_r);
	DECLARE_WRITE16_MEMBER(coins_w);
	DECLARE_READ16_MEMBER(unk_r);
	DECLARE_WRITE16_MEMBER(mux_w);
	DECLARE_READ16_MEMBER(mux_r);
	DECLARE_INPUT_CHANGED_MEMBER(touchscreen_press);
};



/*************************************
 *
 *  Defines
 *
 *************************************/

enum int_levels
{
	INT_6821PIA    = 1,
	INT_TMS34061   = 2,
	INT_6840PTM    = 3,
	INT_6850ACIA   = 4,
	INT_WATCHDOG   = 5,
	INT_FLOPPYCTRL = 6,
	INT_POWERFAIL  = 7,
};


/*************************************
 *
 *  Video hardware
 *
 *************************************/

static void tms_interrupt(running_machine &machine, int state)
{
	cputag_set_input_line(machine, "maincpu", INT_TMS34061, state);
}

static const struct tms34061_interface tms34061intf =
{
	"screen",		/* The screen we are acting on */
	8,				/* VRAM address is (row << rowshift) | col */
	0x40000,		/* Size of video RAM - FIXME: Should be 128kB + 32kB */
	tms_interrupt	/* Interrupt gen callback */
};

WRITE16_MEMBER(jpmsys5_state::sys5_tms34061_w)
{
	int func = (offset >> 19) & 3;
	int row = (offset >> 7) & 0x1ff;
	int col;

	if (func == 0 || func == 2)
		col = offset  & 0xff;
	else
	{
		col = (offset << 1);

		if (~offset & 0x40000)
			row |= 0x200;
	}

	if (ACCESSING_BITS_8_15)
		tms34061_w(&space, col, row, func, data >> 8);

	if (ACCESSING_BITS_0_7)
		tms34061_w(&space, col | 1, row, func, data & 0xff);
}

READ16_MEMBER(jpmsys5_state::sys5_tms34061_r)
{
	UINT16 data = 0;
	int func = (offset >> 19) & 3;
	int row = (offset >> 7) & 0x1ff;
	int col;

	if (func == 0 || func == 2)
		col = offset  & 0xff;
	else
	{
		col = (offset << 1);

		if (~offset & 0x40000)
			row |= 0x200;
	}

	if (ACCESSING_BITS_8_15)
		data |= tms34061_r(&space, col, row, func) << 8;

	if (ACCESSING_BITS_0_7)
		data |= tms34061_r(&space, col | 1, row, func);

	return data;
}

WRITE16_MEMBER(jpmsys5_state::ramdac_w)
{
	if (offset == 0)
	{
		m_pal_addr = data;
		m_pal_idx = 0;
	}
	else if (offset == 1)
	{
		m_palette[m_pal_addr][m_pal_idx] = data;

		if (++m_pal_idx == 3)
		{
			/* Update the MAME palette */
			palette_set_color_rgb(machine(), m_pal_addr, pal6bit(m_palette[m_pal_addr][0]), pal6bit(m_palette[m_pal_addr][1]), pal6bit(m_palette[m_pal_addr][2]));
			m_pal_addr++;
			m_pal_idx = 0;
		}
	}
	else
	{
		/* Colour mask? */
	}
}

static VIDEO_START( jpmsys5v )
{
	tms34061_start(machine, &tms34061intf);
}

static SCREEN_UPDATE_RGB32( jpmsys5v )
{
	int x, y;
	struct tms34061_display state;

	tms34061_get_display_state(&state);

	if (state.blanked)
	{
		bitmap.fill(get_black_pen(screen.machine()), cliprect);
		return 0;
	}

	for (y = cliprect.min_y; y <= cliprect.max_y; ++y)
	{
		UINT8 *src = &state.vram[(state.dispstart & 0xffff)*2 + 256 * y];
		UINT32 *dest = &bitmap.pix32(y, cliprect.min_x);

		for (x = cliprect.min_x; x <= cliprect.max_x; x +=2)
		{
			UINT8 pen = src[(x-cliprect.min_x)>>1];

			/* Draw two 4-bit pixels */
			*dest++ = screen.machine().pens[(pen >> 4) & 0xf];
			*dest++ = screen.machine().pens[pen & 0xf];
		}
	}

	return 0;
}

static void sys5_draw_lamps(jpmsys5_state *state)
{
	int i;
	for (i = 0; i <8; i++)
	{
		output_set_lamp_value( (16*state->m_lamp_strobe)+i,  (state->m_muxram[(4*state->m_lamp_strobe)] & (1 << i)) !=0);
		output_set_lamp_value((16*state->m_lamp_strobe)+i+8, (state->m_muxram[(4*state->m_lamp_strobe) +1 ] & (1 << i)) !=0);
		output_set_indexed_value("sys5led",(8*state->m_lamp_strobe)+i,(state->m_muxram[(4*state->m_lamp_strobe) +2 ] & (1 << i)) !=0);
	}
}

/****************************************
 *
 *  General machine functions
 *
 ****************************************/

WRITE16_MEMBER(jpmsys5_state::rombank_w)
{
	UINT8 *rom = machine().region("maincpu")->base();
	data &= 0x1f;
	subbank("bank1")->set_base(&rom[0x20000 + 0x20000 * data]);
}

READ16_MEMBER(jpmsys5_state::coins_r)
{
	if (offset == 2)
		return input_port_read(machine(), "COINS") << 8;
	else
		return 0xffff;
}

WRITE16_MEMBER(jpmsys5_state::coins_w)
{
	/* TODO */
}

READ16_MEMBER(jpmsys5_state::unk_r)
{
	return 0xffff;
}

WRITE16_MEMBER(jpmsys5_state::mux_w)
{
	m_muxram[offset]=data;
}

READ16_MEMBER(jpmsys5_state::mux_r)
{
	if (offset == 0x81/2)
		return input_port_read(machine(), "DSW");

	return 0xffff;
}

static WRITE16_DEVICE_HANDLER( jpm_upd7759_w )
{
	if (offset == 0)
	{
		upd7759_port_w(device, 0, data & 0xff);
		upd7759_start_w(device, 0);
		upd7759_start_w(device, 1);
	}
	else if (offset == 2)
	{
		upd7759_reset_w(device, ~data & 0x4);
		upd7759_set_bank_base(device, (data & 2) ? 0x20000 : 0);
	}
	else
	{
		logerror("%s: upd7759: Unknown write to %x with %x\n", device->machine().describe_context(),  offset, data);
	}
}

static READ16_DEVICE_HANDLER( jpm_upd7759_r )
{
	return 0x14 | upd7759_busy_r(device);
}


/*************************************
 *
 *  68000 CPU memory handlers
 *
 *************************************/

static ADDRESS_MAP_START( 68000_map, AS_PROGRAM, 16, jpmsys5_state )
	AM_RANGE(0x000000, 0x01ffff) AM_ROM
	AM_RANGE(0x01fffe, 0x01ffff) AM_WRITE(rombank_w)
	AM_RANGE(0x020000, 0x03ffff) AM_ROMBANK("bank1")
	AM_RANGE(0x040000, 0x043fff) AM_RAM AM_SHARE("nvram")
	AM_RANGE(0x046000, 0x046001) AM_WRITENOP
	AM_RANGE(0x046020, 0x046021) AM_DEVREADWRITE8("acia6850_0", acia6850_device, status_read, control_write, 0xff)
	AM_RANGE(0x046022, 0x046023) AM_DEVREADWRITE8("acia6850_0", acia6850_device, data_read, data_write, 0xff)
	AM_RANGE(0x046040, 0x04604f) AM_DEVREADWRITE8("6840ptm", ptm6840_device, read, write, 0xff)
	AM_RANGE(0x046060, 0x046061) AM_READ_PORT("DIRECT") AM_WRITENOP
	AM_RANGE(0x046062, 0x046063) AM_WRITENOP
	AM_RANGE(0x046064, 0x046065) AM_WRITENOP
	AM_RANGE(0x046066, 0x046067) AM_WRITENOP
	AM_RANGE(0x046080, 0x046081) AM_DEVREADWRITE8("acia6850_1", acia6850_device, status_read, control_write, 0xff)
	AM_RANGE(0x046082, 0x046083) AM_DEVREADWRITE8("acia6850_1", acia6850_device, data_read, data_write, 0xff)
	AM_RANGE(0x046084, 0x046085) AM_READ(unk_r) // PIA?
	AM_RANGE(0x046088, 0x046089) AM_READ(unk_r) // PIA?
	AM_RANGE(0x04608c, 0x04608d) AM_DEVREADWRITE8("acia6850_2", acia6850_device, status_read, control_write, 0xff)
	AM_RANGE(0x04608e, 0x04608f) AM_DEVREADWRITE8("acia6850_2", acia6850_device, data_read, data_write, 0xff)
	AM_RANGE(0x0460a0, 0x0460a3) AM_DEVWRITE8_LEGACY("ym2413", ym2413_w, 0x00ff)
	AM_RANGE(0x0460c0, 0x0460c1) AM_WRITENOP
	AM_RANGE(0x0460e0, 0x0460e5) AM_WRITE(ramdac_w)
	AM_RANGE(0x048000, 0x04801f) AM_READWRITE(coins_r, coins_w)
	AM_RANGE(0x04c000, 0x04c0ff) AM_READ(mux_r) AM_WRITE(mux_w)
	AM_RANGE(0x04c100, 0x04c105) AM_DEVREADWRITE_LEGACY("upd7759", jpm_upd7759_r, jpm_upd7759_w)
	AM_RANGE(0x800000, 0xcfffff) AM_READWRITE(sys5_tms34061_r, sys5_tms34061_w)
ADDRESS_MAP_END


 /*************************************
 *
 *  Touchscreen controller simulation
 *
 *************************************/

/* Serial bit transmission callback */
static TIMER_CALLBACK( touch_cb )
{
	jpmsys5_state *state = machine.driver_data<jpmsys5_state>();
	switch (state->m_touch_state)
	{
		case IDLE:
		{
			break;
		}
		case START:
		{
			state->m_touch_shift_cnt = 0;
			state->m_a2_data_in = 0;
			state->m_touch_state = DATA;
			break;
		}
		case DATA:
		{
			state->m_a2_data_in = (state->m_touch_data[state->m_touch_data_count] >> (state->m_touch_shift_cnt)) & 1;

			if (++state->m_touch_shift_cnt == 8)
				state->m_touch_state = STOP1;

			break;
		}
		case STOP1:
		{
			state->m_a2_data_in = 1;
			state->m_touch_state = STOP2;
			break;
		}
		case STOP2:
		{
			state->m_a2_data_in = 1;

			if (++state->m_touch_data_count == 3)
			{
				state->m_touch_timer->reset();
				state->m_touch_state = IDLE;
			}
			else
			{
				state->m_touch_state = START;
			}

			break;
		}
	}
}

INPUT_CHANGED_MEMBER(jpmsys5_state::touchscreen_press)
{
	if (newval == 0)
	{
		attotime rx_period = attotime::from_hz(10000) * 16;

		/* Each touch screen packet is 3 bytes */
		m_touch_data[0] = 0x2a;
		m_touch_data[1] = 0x7 - (input_port_read(machine(), "TOUCH_Y") >> 5) + 0x30;
		m_touch_data[2] = (input_port_read(machine(), "TOUCH_X") >> 5) + 0x30;

		/* Start sending the data to the 68000 serially */
		m_touch_data_count = 0;
		m_touch_state = START;
		m_touch_timer->adjust(rx_period, 0, rx_period);
	}
}


/*************************************
 *
 *  Port definitions
 *
 *************************************/

static INPUT_PORTS_START( monopoly )
	PORT_START("DSW")
	PORT_DIPNAME( 0x01, 0x01, "Change state to enter test" ) PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(	0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unused ) ) PORT_DIPLOCATION("SW2:2")
	PORT_DIPSETTING(	0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, "Alarm" ) PORT_DIPLOCATION("SW2:3")
	PORT_DIPSETTING(	0x04, "Discontinue alarm when jam is cleared" )
	PORT_DIPSETTING(	0x00, "Continue alarm until back door open" )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unused ) ) PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(	0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Unused ) ) PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(	0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unused ) ) PORT_DIPLOCATION("SW2:6")
	PORT_DIPSETTING(	0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0xc0, 0x00, "Payout Percentage" ) PORT_DIPLOCATION("SW2:7,8")
	PORT_DIPSETTING(	0x00, "50%" )
	PORT_DIPSETTING(	0x80, "45%" )
	PORT_DIPSETTING(	0x40, "40%" )
	PORT_DIPSETTING(	0xc0, "30%" )

	PORT_START("DIRECT")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE ) PORT_NAME("Back door") PORT_CODE(KEYCODE_R) PORT_TOGGLE
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_SERVICE ) PORT_NAME("Cash door") PORT_CODE(KEYCODE_T) PORT_TOGGLE
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_SERVICE ) PORT_NAME("Refill key") PORT_CODE(KEYCODE_Y) PORT_TOGGLE
	PORT_DIPNAME( 0x08, 0x08, DEF_STR ( Unknown ) )
	PORT_DIPSETTING(	0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR ( Unknown ) )
	PORT_DIPSETTING(	0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR ( Unknown ) )
	PORT_DIPSETTING(	0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, "Reset" ) PORT_DIPLOCATION("SW1:1")
	PORT_DIPSETTING(	0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x00, DEF_STR ( Unknown ) )
	PORT_DIPSETTING(	0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )

	PORT_START("COINS")
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 ) PORT_NAME("10p")
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 ) PORT_NAME("20p")
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_COIN3 ) PORT_NAME("50p")
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_COIN4 ) PORT_NAME("100p")
	PORT_BIT( 0xc3, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("TOUCH_PUSH")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_CHANGED_MEMBER(DEVICE_SELF, jpmsys5_state,touchscreen_press, NULL)

	PORT_START("TOUCH_X")
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_X ) PORT_CROSSHAIR(X, 1.0, 0.0, 0) PORT_SENSITIVITY(45) PORT_KEYDELTA(15)

	PORT_START("TOUCH_Y")
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_Y ) PORT_CROSSHAIR(Y, 1.0, 0.0, 0) PORT_SENSITIVITY(45) PORT_KEYDELTA(15)
INPUT_PORTS_END


/*************************************
 *
 *  6840 PTM
 *
 *************************************/

static WRITE_LINE_DEVICE_HANDLER( ptm_irq )
{
	cputag_set_input_line(device->machine(), "maincpu", INT_6840PTM, state ? ASSERT_LINE : CLEAR_LINE);
}

static WRITE8_DEVICE_HANDLER(u26_o1_callback)
{
	jpmsys5_state *state = device->machine().driver_data<jpmsys5_state>();
	if (state->m_mpxclk !=data)
	{
		if (!data) //falling edge
		{
			state->m_lamp_strobe++;
			if (state->m_lamp_strobe >15)
			{
				state->m_lamp_strobe =0;
			}
		}
		sys5_draw_lamps(state);
	}
	state->m_mpxclk = data;
}


static const ptm6840_interface ptm_intf =
{
	1000000,
	{ 0, 0, 0 },
	{ DEVCB_HANDLER(u26_o1_callback), DEVCB_NULL, DEVCB_NULL },
	DEVCB_LINE(ptm_irq)
};


/*************************************
 *
 *  6850 ACIAs
 *
 *************************************/

static WRITE_LINE_DEVICE_HANDLER( acia_irq )
{
	cputag_set_input_line(device->machine(), "maincpu", INT_6850ACIA, state ? CLEAR_LINE : ASSERT_LINE);
}

/* Clocks are incorrect */

static READ_LINE_DEVICE_HANDLER( a0_rx_r )
{
	jpmsys5_state *state = device->machine().driver_data<jpmsys5_state>();
	return state->m_a0_data_in;
}

static WRITE_LINE_DEVICE_HANDLER( a0_tx_w )
{
	jpmsys5_state *drvstate = device->machine().driver_data<jpmsys5_state>();
	drvstate->m_a0_data_out = state;
}

static READ_LINE_DEVICE_HANDLER( a0_dcd_r )
{
	jpmsys5_state *state = device->machine().driver_data<jpmsys5_state>();
	return state->m_a0_acia_dcd;
}

static ACIA6850_INTERFACE( acia0_if )
{
	10000,
	10000,
	DEVCB_LINE(a0_rx_r), /*&a0_data_in,*/
	DEVCB_LINE(a0_tx_w), /*&a0_data_out,*/
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_LINE(a0_dcd_r), /*&a0_acia_dcd,*/
	DEVCB_LINE(acia_irq)
};

static READ_LINE_DEVICE_HANDLER( a1_rx_r )
{
	jpmsys5_state *state = device->machine().driver_data<jpmsys5_state>();
	return state->m_a1_data_in;
}

static WRITE_LINE_DEVICE_HANDLER( a1_tx_w )
{
	jpmsys5_state *drvstate = device->machine().driver_data<jpmsys5_state>();
	drvstate->m_a1_data_out = state;
}

static READ_LINE_DEVICE_HANDLER( a1_dcd_r )
{
	jpmsys5_state *state = device->machine().driver_data<jpmsys5_state>();
	return state->m_a1_acia_dcd;
}

static ACIA6850_INTERFACE( acia1_if )
{
	10000,
	10000,
	DEVCB_LINE(a1_rx_r), /*&state->m_a1_data_in,*/
	DEVCB_LINE(a1_tx_w), /*&state->m_a1_data_out,*/
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_LINE(a1_dcd_r), /*&state->m_a1_acia_dcd,*/
	DEVCB_LINE(acia_irq)
};

static READ_LINE_DEVICE_HANDLER( a2_rx_r )
{
	jpmsys5_state *state = device->machine().driver_data<jpmsys5_state>();
	return state->m_a2_data_in;
}

static WRITE_LINE_DEVICE_HANDLER( a2_tx_w )
{
	jpmsys5_state *drvstate = device->machine().driver_data<jpmsys5_state>();
	drvstate->m_a2_data_out = state;
}

static READ_LINE_DEVICE_HANDLER( a2_dcd_r )
{
	jpmsys5_state *state = device->machine().driver_data<jpmsys5_state>();
	return state->m_a2_acia_dcd;
}

static ACIA6850_INTERFACE( acia2_if )
{
	10000,
	10000,
	DEVCB_LINE(a2_rx_r), /*&state->m_a2_data_in,*/
	DEVCB_LINE(a2_tx_w), /*&state->m_a2_data_out,*/
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_LINE(a2_dcd_r), /*&state->m_a2_acia_dcd,*/
	DEVCB_LINE(acia_irq)
};


/*************************************
 *
 *  Initialisation
 *
 *************************************/

static MACHINE_START( jpmsys5v )
{
	jpmsys5_state *state = machine.driver_data<jpmsys5_state>();
	state->subbank("bank1")->set_base(machine.region("maincpu")->base()+0x20000);
	state->m_touch_timer = machine.scheduler().timer_alloc(FUNC(touch_cb));
}

static MACHINE_RESET( jpmsys5v )
{
	jpmsys5_state *state = machine.driver_data<jpmsys5_state>();
	state->m_touch_timer->reset();
	state->m_touch_state = IDLE;
	state->m_a2_data_in = 1;
	state->m_a2_acia_dcd = 0;
}


/*************************************
 *
 *  Machine driver
 *
 *************************************/

static MACHINE_CONFIG_START( jpmsys5v, jpmsys5_state )
	MCFG_CPU_ADD("maincpu", M68000, 8000000)
	MCFG_CPU_PROGRAM_MAP(68000_map)

	MCFG_ACIA6850_ADD("acia6850_0", acia0_if)
	MCFG_ACIA6850_ADD("acia6850_1", acia1_if)
	MCFG_ACIA6850_ADD("acia6850_2", acia2_if)

	MCFG_NVRAM_ADD_0FILL("nvram")

	MCFG_MACHINE_START(jpmsys5v)
	MCFG_MACHINE_RESET(jpmsys5v)

	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_RAW_PARAMS(XTAL_40MHz / 4, 676, 20*4, 147*4, 256, 0, 254)
	MCFG_SCREEN_UPDATE_STATIC(jpmsys5v)

	MCFG_VIDEO_START(jpmsys5v)

	MCFG_PALETTE_LENGTH(16)

	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_ADD("upd7759", UPD7759, UPD7759_STANDARD_CLOCK)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.30)

	/* Earlier revisions use an SAA1099 */
	MCFG_SOUND_ADD("ym2413", YM2413, 4000000 ) /* Unconfirmed */
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	/* 6840 PTM */
	MCFG_PTM6840_ADD("6840ptm", ptm_intf)
MACHINE_CONFIG_END



static ADDRESS_MAP_START( 68000_awp_map, AS_PROGRAM, 16, jpmsys5_state )
	AM_RANGE(0x000000, 0x01ffff) AM_ROM
	AM_RANGE(0x01fffe, 0x01ffff) AM_WRITE(rombank_w)
	AM_RANGE(0x020000, 0x03ffff) AM_ROMBANK("bank1")
	AM_RANGE(0x040000, 0x043fff) AM_RAM AM_SHARE("nvram")
	AM_RANGE(0x046000, 0x046001) AM_WRITENOP
	AM_RANGE(0x046020, 0x046021) AM_DEVREADWRITE8("acia6850_0", acia6850_device, status_read, control_write, 0xff)
	AM_RANGE(0x046022, 0x046023) AM_DEVREADWRITE8("acia6850_0", acia6850_device, data_read, data_write, 0xff)
	AM_RANGE(0x046040, 0x04604f) AM_DEVREADWRITE8("6840ptm", ptm6840_device, read, write, 0xff)
	AM_RANGE(0x046060, 0x046061) AM_READ_PORT("DIRECT") AM_WRITENOP
	AM_RANGE(0x046062, 0x046063) AM_WRITENOP
	AM_RANGE(0x046064, 0x046065) AM_WRITENOP
	AM_RANGE(0x046066, 0x046067) AM_WRITENOP
	AM_RANGE(0x046080, 0x046081) AM_DEVREADWRITE8("acia6850_1", acia6850_device, status_read, control_write, 0xff)
	AM_RANGE(0x046082, 0x046083) AM_DEVREADWRITE8("acia6850_1", acia6850_device, data_read, data_write, 0xff)
	AM_RANGE(0x046084, 0x046085) AM_READ(unk_r) // PIA?
	AM_RANGE(0x046088, 0x046089) AM_READ(unk_r) // PIA?
	AM_RANGE(0x04608c, 0x04608d) AM_DEVREADWRITE8("acia6850_2", acia6850_device, status_read, control_write, 0xff)
	AM_RANGE(0x04608e, 0x04608f) AM_DEVREADWRITE8("acia6850_2", acia6850_device, data_read, data_write, 0xff)
	AM_RANGE(0x0460a0, 0x0460a3) AM_DEVWRITE8_LEGACY("ym2413", ym2413_w, 0x00ff)
	AM_RANGE(0x0460c0, 0x0460c1) AM_WRITENOP
	AM_RANGE(0x048000, 0x04801f) AM_READWRITE(coins_r, coins_w)
	AM_RANGE(0x04c000, 0x04c0ff) AM_READ(mux_r) AM_WRITE(mux_w)
	AM_RANGE(0x04c100, 0x04c105) AM_DEVREADWRITE_LEGACY("upd7759", jpm_upd7759_r, jpm_upd7759_w)
ADDRESS_MAP_END

/*************************************
 *
 *  Port definitions
 *
 *************************************/

static INPUT_PORTS_START( popeye )
	PORT_START("DSW")
	PORT_DIPNAME( 0x01, 0x01, "Change state to enter test" ) PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(	0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unused ) ) PORT_DIPLOCATION("SW2:2")
	PORT_DIPSETTING(	0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, "Alarm" ) PORT_DIPLOCATION("SW2:3")
	PORT_DIPSETTING(	0x04, "Discontinue alarm when jam is cleared" )
	PORT_DIPSETTING(	0x00, "Continue alarm until back door open" )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unused ) ) PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(	0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Unused ) ) PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(	0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unused ) ) PORT_DIPLOCATION("SW2:6")
	PORT_DIPSETTING(	0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0xc0, 0x00, "Payout Percentage" ) PORT_DIPLOCATION("SW2:7,8")
	PORT_DIPSETTING(	0x00, "50%" )
	PORT_DIPSETTING(	0x80, "45%" )
	PORT_DIPSETTING(	0x40, "40%" )
	PORT_DIPSETTING(	0xc0, "30%" )

	PORT_START("DIRECT")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE ) PORT_NAME("Back door") PORT_CODE(KEYCODE_R) PORT_TOGGLE
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_SERVICE ) PORT_NAME("Cash door") PORT_CODE(KEYCODE_T) PORT_TOGGLE
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_SERVICE ) PORT_NAME("Refill key") PORT_CODE(KEYCODE_Y) PORT_TOGGLE
	PORT_DIPNAME( 0x08, 0x08, DEF_STR ( Unknown ) )
	PORT_DIPSETTING(	0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR ( Unknown ) )
	PORT_DIPSETTING(	0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR ( Unknown ) )
	PORT_DIPSETTING(	0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, "Reset" ) PORT_DIPLOCATION("SW1:1")
	PORT_DIPSETTING(	0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x00, DEF_STR ( Unknown ) )
	PORT_DIPSETTING(	0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(	0x00, DEF_STR( On ) )

	PORT_START("COINS")
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 ) PORT_NAME("10p")
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 ) PORT_NAME("20p")
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_COIN3 ) PORT_NAME("50p")
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_COIN4 ) PORT_NAME("100p")
	PORT_BIT( 0xc3, IP_ACTIVE_LOW, IPT_UNUSED )

INPUT_PORTS_END

/*************************************
 *
 *  Initialisation
 *
 *************************************/

static MACHINE_START( jpmsys5 )
{
	machine.root_device().subbank("bank1")->set_base(machine.region("maincpu")->base()+0x20000);
}

static MACHINE_RESET( jpmsys5 )
{
	jpmsys5_state *state = machine.driver_data<jpmsys5_state>();
	state->m_a2_data_in = 1;
	state->m_a2_acia_dcd = 0;
}

/*************************************
 *
 *  Machine driver
 *
 *************************************/

static MACHINE_CONFIG_START( jpmsys5, jpmsys5_state )
	MCFG_CPU_ADD("maincpu", M68000, 8000000)
	MCFG_CPU_PROGRAM_MAP(68000_awp_map)

	MCFG_ACIA6850_ADD("acia6850_0", acia0_if)
	MCFG_ACIA6850_ADD("acia6850_1", acia1_if)
	MCFG_ACIA6850_ADD("acia6850_2", acia2_if)

	MCFG_NVRAM_ADD_0FILL("nvram")

	MCFG_MACHINE_START(jpmsys5)
	MCFG_MACHINE_RESET(jpmsys5)
	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_ADD("upd7759", UPD7759, UPD7759_STANDARD_CLOCK)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.30)

	/* Earlier revisions use an SAA1099 */
	MCFG_SOUND_ADD("ym2413", YM2413, 4000000 ) /* Unconfirmed */
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	/* 6840 PTM */
	MCFG_PTM6840_ADD("6840ptm", ptm_intf)
	MCFG_DEFAULT_LAYOUT(layout_awpvid16)
MACHINE_CONFIG_END

/*************************************
 *
 *  ROM definition(s)
 *
 *************************************/

ROM_START( monopoly )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7398.bin", 0x000000, 0x80000, CRC(62c80f20) SHA1(322514f920d6cb48887b624786b52af34bdb8e5f) )
	ROM_LOAD16_BYTE( "7399.bin", 0x000001, 0x80000, CRC(5f410eb6) SHA1(f9949b5cba64db77187c1723a52570bdb182ce5c) )
	ROM_LOAD16_BYTE( "6668.bin", 0x100000, 0x80000, CRC(30bf082a) SHA1(29ba67a86e82f0eb4feb816a2031d62028eb11b0) )
	ROM_LOAD16_BYTE( "6669.bin", 0x100001, 0x80000, CRC(85d38c2d) SHA1(2f1a394df243e5fbbad31507b9074c997c473106) )
	ROM_LOAD16_BYTE( "6670.bin", 0x200000, 0x80000, CRC(66e2a5e1) SHA1(04d4b55d6ad121cdc3592d33e9d953affa24f01a) )
	ROM_LOAD16_BYTE( "6671.bin", 0x200001, 0x80000, CRC(b2a3cedd) SHA1(e3a5dd028b0769e08a796a96665b31491c3b18ca) )



	ROM_REGION( 0x300000, "altrevs", 0 )
	ROM_LOAD16_BYTE( "7400.bin", 0x00001, 0x080000, CRC(d6f1f98c) SHA1(f20c788a31a8fe339aed701866180a3eb16fafb9) )

	// this version doesn't work too well, missing other roms, or just bad?
	ROM_LOAD16_BYTE( "monov3p1", 0x00000, 0x080000, CRC(a66fc610) SHA1(fddd3b37a6aebf5c402942d26a2fa1fa130326dd) )
	ROM_LOAD16_BYTE( "monov3p2", 0x00001, 0x080000, CRC(2d629723) SHA1(c5584113e50dc5f636dbcf80e4689d2bbfe98e71) )
	// mismastched?
	ROM_LOAD16_BYTE( "monov4p2", 0x00001, 0x080000, CRC(3c2dd9b7) SHA1(01c87584b3599763a0c37040199014c2902dc6f3) )


	ROM_REGION( 0x80000, "upd7759", 0 )
	ROM_LOAD( "6538.bin", 0x00000, 0x40000, CRC(ccdd4ce3) SHA1(dbb24682cea8081a447ca2c53395964fc46e7f56) )
ROM_END

ROM_START( monopolya )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "mono4e1",  0x000000, 0x80000, CRC(0338b165) SHA1(fdc0fcf0fddcf88d593a22885779e8224484e7e4) )
	ROM_LOAD16_BYTE( "mono4e2",  0x000001, 0x80000, CRC(c8aa21d8) SHA1(257ecf85e1d41b15bb2bbe2157e9d3f72b7e0317) )
	ROM_LOAD16_BYTE( "6668.bin", 0x100000, 0x80000, CRC(30bf082a) SHA1(29ba67a86e82f0eb4feb816a2031d62028eb11b0) )
	ROM_LOAD16_BYTE( "6669.bin", 0x100001, 0x80000, CRC(85d38c2d) SHA1(2f1a394df243e5fbbad31507b9074c997c473106) )
	ROM_LOAD16_BYTE( "6670.bin", 0x200000, 0x80000, CRC(66e2a5e1) SHA1(04d4b55d6ad121cdc3592d33e9d953affa24f01a) )
	ROM_LOAD16_BYTE( "6671.bin", 0x200001, 0x80000, CRC(b2a3cedd) SHA1(e3a5dd028b0769e08a796a96665b31491c3b18ca) )

	ROM_REGION( 0x80000, "upd7759", 0 )
	ROM_LOAD( "6538.bin", 0x00000, 0x40000, CRC(ccdd4ce3) SHA1(dbb24682cea8081a447ca2c53395964fc46e7f56) )
ROM_END


ROM_START( monoplcl )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7401.bin", 0x000000, 0x80000, CRC(eec11426) SHA1(b732a5a64d3fba676134942768b823d088792a1f) )
	ROM_LOAD16_BYTE( "7402.bin", 0x000001, 0x80000, CRC(c4c43269) SHA1(3cad3a66aae25308e8709f8eb3f29d6858b87791) )
	ROM_LOAD16_BYTE( "6668.bin", 0x100000, 0x80000, CRC(30bf082a) SHA1(29ba67a86e82f0eb4feb816a2031d62028eb11b0) )
	ROM_LOAD16_BYTE( "6669.bin", 0x100001, 0x80000, CRC(85d38c2d) SHA1(2f1a394df243e5fbbad31507b9074c997c473106) )
	ROM_LOAD16_BYTE( "6670.bin", 0x200000, 0x80000, CRC(66e2a5e1) SHA1(04d4b55d6ad121cdc3592d33e9d953affa24f01a) )
	ROM_LOAD16_BYTE( "6671.bin", 0x200001, 0x80000, CRC(b2a3cedd) SHA1(e3a5dd028b0769e08a796a96665b31491c3b18ca) )

	ROM_REGION( 0x300000, "altrevs", 0 )
	ROM_LOAD16_BYTE( "7403.bin", 0x000001, 0x080000, CRC(95dbacb6) SHA1(bd551ccad95440a669a547092ab126178b0d0bf9) )

	ROM_LOAD( "mdlxv1p1", 0x0000, 0x080000, CRC(48ab1691) SHA1(6df2aad02548d5239e3974a11228bc9aad8c9170) )
	ROM_LOAD( "mdlxv1p2", 0x0000, 0x080000, CRC(107c3e65) SHA1(e298b3a2826f92ba6119348a36bc4735e1799797) )
	/* p3 missing? */
	ROM_LOAD( "mdlxv1p4", 0x0000, 0x080000, CRC(e3fd1a27) SHA1(6bba70ff27a6d068febcbdfa1b1f8ff2ef86ef03) )


	ROM_REGION( 0x80000, "upd7759", 0 )
	ROM_LOAD( "6538.bin", 0x00000, 0x40000, CRC(ccdd4ce3) SHA1(dbb24682cea8081a447ca2c53395964fc46e7f56) )
ROM_END

ROM_START( monopldx )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8439.bin", 0x000000, 0x80000, CRC(fbd6caa4) SHA1(73e787ae41a0ce44d48a46dd623d5e1351335e3e) )
	ROM_LOAD16_BYTE( "8440.bin", 0x000001, 0x80000, CRC(4e20aebf) SHA1(79aca78f023e7f7ae7875c18c3a7696f5ab63102) )
	ROM_LOAD16_BYTE( "6879.bin", 0x100000, 0x80000, CRC(4fbd1222) SHA1(9a9c9e4768c18a6a3e717605d3c88179676b6ad1) )
	ROM_LOAD16_BYTE( "6880.bin", 0x100001, 0x80000, CRC(0370bf5f) SHA1(a0ed1dbc6aeab02e8229f23f8ba4ff880d31e7a1) )
	ROM_LOAD16_BYTE( "6881.bin", 0x200000, 0x80000, CRC(8418ee17) SHA1(5666b90db00d9e88a37655bb9a714f076e2689d6) )
	ROM_LOAD16_BYTE( "6882.bin", 0x200001, 0x80000, CRC(400f5fb4) SHA1(80b1d3902fc9f6db24f49055b07bc31c0c74a993) )

	ROM_REGION( 0x300000, "altrevs", 0 )
	ROM_LOAD16_BYTE( "8441.bin", 0x000001, 0x080000, CRC(d0825af4) SHA1(a7291806893c42a115763e404337976b8c30e9e0) ) // 1 byte change from 8440 (doesn't boot, but might want additional hw)


	ROM_REGION( 0x80000, "upd7759", 0 )
	ROM_LOAD( "modl-snd.bin", 0x000000, 0x80000, CRC(f761da41) SHA1(a07d1b4cb7ce7a24b6fb84843543b95c3aec470f) )
ROM_END


ROM_START( cashcade )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "cashcade_2_1.bin", 0x000000, 0x010000, CRC(7672953c) SHA1(c1a2639ab6b830c971c2533d837404ae5f5aa535) )
	ROM_LOAD16_BYTE( "cashcade_2_2.bin", 0x000001, 0x010000, CRC(8ce8cd66) SHA1(4eb00af6a0260496950d04fdcc1d3d976868ce3e) )
	ROM_LOAD16_BYTE( "cashcade_2_3.bin", 0x020000, 0x010000, CRC(a4caddd1) SHA1(074e4aa870c3d28c2f120936ef6928c3b5e14301) )
	ROM_LOAD16_BYTE( "cashcade_2_4.bin", 0x020001, 0x010000, CRC(b0f595e8) SHA1(5ca12839b87d092504d8b7cc579b8f1b2406cea1) )
ROM_END





/* Non-video */


ROM_START( j5goldbr )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jpmgoldenbars1.1.bin", 0x000000, 0x008000, CRC(45f91660) SHA1(1c6bc864e56c8c6ea61ebb5e181ad736aeab06cf) )
	ROM_LOAD16_BYTE( "jpmgoldenbars1.2.bin", 0x000001, 0x008000, CRC(eb6595f0) SHA1(2b0aabb50a1d1f88249b733faf02194c0181f999) )
	ROM_LOAD16_BYTE( "jpmgoldenbars1.3.bin", 0x020000, 0x008000, CRC(01c7dcfb) SHA1(9f00a14df5b2ea13d2bd4f3ff1ab5ee65d464709) )
	ROM_LOAD16_BYTE( "jpmgoldenbars1.4.bin", 0x020001, 0x008000, CRC(88bf0d26) SHA1(ecbfa69ffde42dc4464f39fc641c98a8485e0218) )
ROM_END

ROM_START( j5fifth )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5av.91",  0x000000, 0x008000, CRC(d932dcb7) SHA1(c1ccdbc19c7904e35a905c51897e938e8e304a00) )
	ROM_LOAD16_BYTE( "5av.9a2", 0x000001, 0x008000, CRC(bd6aa8ed) SHA1(9271b20e19d83289523ced79d995a95ee74f9cce) )
ROM_END



// this came from the 'Crystal' Club set, but was also in the 'Crystal' normal set (which looks more like a club set).. Maybe it's just the club sound rom.
#define J5AR80CL_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "atwcsnd.bin", 0x0000, 0x040000, CRC(c637b1ce) SHA1(e68a3f390f3671af693f080f20119d54118e10f0) ) \


ROM_START( j5ar80cl )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "do10_1.bin", 0x000000, 0x010000, CRC(c1b6d961) SHA1(0e103edc31e8a1b98b3daab6604a13490dcaa56e) )
	ROM_LOAD16_BYTE( "do10_2.bin", 0x000001, 0x010000, CRC(113b228c) SHA1(2087ad14c620d9d1aa853f9ab56265c7efaa9a2c) )

	J5AR80CL_SOUND
ROM_END

ROM_START( j5ar80cla ) // marked as a 'Crystal' Club set
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "atw_club_v6_p1.bin", 0x000000, 0x010000, CRC(340c2938) SHA1(00b2dcbff2b551be8936efa7839e677d98660f69) )
	ROM_LOAD16_BYTE( "atw_club_v6_p2.bin", 0x000001, 0x010000, CRC(b1627dd9) SHA1(6db1241f0ecd6162289d5b1080ade91b2a945572) )

	J5AR80CL_SOUND
ROM_END

ROM_START( j5ar80clb ) // marked as a 'Crystal' Club set
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "atw_club_v8_1.bin", 0x000000, 0x010000, CRC(a286d1b7) SHA1(309c21a69cf93e8201910d18ccb19e12f6fcaad3) )
	ROM_LOAD16_BYTE( "atw_club_v8_2.bin", 0x000001, 0x010000, CRC(6088e013) SHA1(86201fec761551c562da1ec0f3c04b7780c46f4c) )

	J5AR80CL_SOUND
ROM_END

ROM_START( j5ar80clc ) // marked as a 'Crystal' non-club set, but the Rom size etc. suggest otherwise
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "atw8v9p1", 0x000000, 0x010000, CRC(3d2a5534) SHA1(469b4ed46ab5cb67909c14e5e71c632bcd2b14e3) )
	ROM_LOAD16_BYTE( "awt8v9p2", 0x000001, 0x010000, CRC(87207023) SHA1(5bda03a8964275528b5aa32ec6819b5e9bcbcac1) )

	J5AR80CL_SOUND
ROM_END


#define J5AR80_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "5652.bin", 0x000000, 0x040000, CRC(d0876512) SHA1(1bda1d640ca5ee6831d7a4ae948e3dce277e8a3e) )	 \

//  ROM_LOAD( "atw80snd.bin", 0x000000, 0x020000, CRC(b002e11c) SHA1(f7133f4bb8c31feaad0a7b9ee88749f9b7877575) ) // this is just the first half of 5652/atworldsound.bin

ROM_START( j5ar80 )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6251.bin", 0x000000, 0x008000, CRC(8cd127d4) SHA1(cf269ab025de2b5ae15b66e8a5c0141020ec0559) )
	ROM_LOAD16_BYTE( "6252.bin", 0x000001, 0x008000, CRC(91c2fa9e) SHA1(15f262304955560040822d71ddbda48f18b37339) )
	ROM_LOAD16_BYTE( "6253.bin", 0x010000, 0x008000, CRC(8682a834) SHA1(12343ba67475111a96b6330d47c141bb3ded2a42) )
	ROM_LOAD16_BYTE( "6254.bin", 0x010001, 0x008000, CRC(eed8c270) SHA1(d5c04e85d0debc40c4626530cb442202edc75d8c) )
	J5AR80_SOUND
ROM_END

ROM_START( j5ar80a )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6255.bin", 0x000000, 0x008000, CRC(bf0f5ef5) SHA1(47fa78ea2db0447d744acb29a39532789d744ecf) )
	ROM_LOAD16_BYTE( "6252.bin", 0x000001, 0x008000, CRC(91c2fa9e) SHA1(15f262304955560040822d71ddbda48f18b37339) )
	ROM_LOAD16_BYTE( "6253.bin", 0x010000, 0x008000, CRC(8682a834) SHA1(12343ba67475111a96b6330d47c141bb3ded2a42) )
	ROM_LOAD16_BYTE( "6254.bin", 0x010001, 0x008000, CRC(eed8c270) SHA1(d5c04e85d0debc40c4626530cb442202edc75d8c) )
	J5AR80_SOUND
ROM_END

ROM_START( j5ar80b )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6288.bin", 0x000000, 0x008000, CRC(cd54575f) SHA1(e103a335d0d4722115ab56064be98d8ab237d1e3) )
	ROM_LOAD16_BYTE( "6252.bin", 0x000001, 0x008000, CRC(91c2fa9e) SHA1(15f262304955560040822d71ddbda48f18b37339) )
	ROM_LOAD16_BYTE( "6253.bin", 0x010000, 0x008000, CRC(8682a834) SHA1(12343ba67475111a96b6330d47c141bb3ded2a42) )
	ROM_LOAD16_BYTE( "6254.bin", 0x010001, 0x008000, CRC(eed8c270) SHA1(d5c04e85d0debc40c4626530cb442202edc75d8c) )
	J5AR80_SOUND
ROM_END

ROM_START( j5ar80c )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6289.bin", 0x000000, 0x008000, CRC(eb6dd596) SHA1(043808709f2c3616e319221d4146b2b57a486d38) )
	ROM_LOAD16_BYTE( "6252.bin", 0x000001, 0x008000, CRC(91c2fa9e) SHA1(15f262304955560040822d71ddbda48f18b37339) )
	ROM_LOAD16_BYTE( "6253.bin", 0x010000, 0x008000, CRC(8682a834) SHA1(12343ba67475111a96b6330d47c141bb3ded2a42) )
	ROM_LOAD16_BYTE( "6254.bin", 0x010001, 0x008000, CRC(eed8c270) SHA1(d5c04e85d0debc40c4626530cb442202edc75d8c) )
	J5AR80_SOUND
ROM_END

ROM_START( j5ar80d )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "aw8020p1", 0x000000, 0x008000, CRC(ae75424d) SHA1(6f9372c2ae4e506f6d7009623e9795fb67b3d664) )
	ROM_LOAD16_BYTE( "aw8020p2", 0x000001, 0x008000, CRC(e7ccbcb1) SHA1(696c8fe94b41d6db1e156a9ec8ef22b953428478) )
	ROM_LOAD16_BYTE( "aw8020p3", 0x010000, 0x008000, CRC(2f0c3549) SHA1(3afb86990106ded1abbf19a747e895f75d1e2ecd) )
	ROM_LOAD16_BYTE( "aw8020p4", 0x010001, 0x008000, CRC(17ff1c66) SHA1(9c917eb234a4272f2376ad34a9cdcdbdd7eb9deb) )
	J5AR80_SOUND
ROM_END

// also in these sets
//  ROM_LOAD( "artwld80", 0x000000, 0x010000, CRC(3ff314c3) SHA1(345df80243953b35916449b0aa6ffaf9d3501d2b) ) // something by Crystal Leisure, 1994 (MPU4?)



ROM_START( j5buc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "bucc_5_1.bin", 0x000000, 0x010000, CRC(acbda693) SHA1(87dc3b2f59c1911bde07e7ec5a8d7f7deb699b51) )
	ROM_LOAD16_BYTE( "bucc_5_2.bin", 0x000001, 0x010000, CRC(3bcf34a5) SHA1(66d654980eb576138c3b09c1965afe02a84db001) )
ROM_END




#define J5CIR_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "circ_snd.bin", 0x000000, 0x080000, CRC(a4402d73) SHA1(e1760462734b8529f9ba374c36f9e0f2aa66264f) ) \

ROM_START( j5cir )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "circus_10_quid_p1.bin", 0x000000, 0x010000, CRC(9ac0cc40) SHA1(2c45386d880df6ce3f40c5f74836f91541eb7f71) )
	ROM_LOAD16_BYTE( "circus_10_quid_p2.bin", 0x000001, 0x010000, CRC(d23c25e6) SHA1(d72469258ca2cd044bc6868bac826a9452903359) )
	J5CIR_SOUND
ROM_END


ROM_START( j5cira )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "circ_p1.bin", 0x000000, 0x010000, CRC(da841a46) SHA1(28320a169a22e61255ac33af87f83ab0d55ab335) )
	ROM_LOAD16_BYTE( "circ_p2.bin", 0x000001, 0x010000, CRC(1704862c) SHA1(1e388356ed7df7de345d6d2993bc7e36b0a51770) )
	J5CIR_SOUND
ROM_END


ROM_START( j5cirb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "circusp1", 0x000000, 0x010000, CRC(eed558a3) SHA1(59c454d8a6830a743865eb001689154927adf931) )
	ROM_LOAD16_BYTE( "circusp2", 0x000001, 0x010000, CRC(1704862c) SHA1(1e388356ed7df7de345d6d2993bc7e36b0a51770) )
	J5CIR_SOUND
ROM_END


ROM_START( j5circ )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "circ10p1", 0x000000, 0x010000, CRC(982ef193) SHA1(a563bf5ba03e47ae34e3ef45062b6877c7bb8253) )
	ROM_LOAD16_BYTE( "circ10p2", 0x000001, 0x010000, CRC(2c510a30) SHA1(0e8c7a90bb7f20f39420b1e9f956119ce47ee129) )
	J5CIR_SOUND
ROM_END


ROM_START( j5cird )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "circ8ac", 0x000000, 0x010000, CRC(ac7fb376) SHA1(9dcbb1d27cbcdd7085b8e0d926a099fddabe6fc8) )
	ROM_LOAD16_BYTE( "circ8p2", 0x000001, 0x010000, CRC(2c510a30) SHA1(0e8c7a90bb7f20f39420b1e9f956119ce47ee129) )
	J5CIR_SOUND
ROM_END




ROM_START( j5clbnud )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "clubnudger4.1.bin", 0x000000, 0x008000, CRC(5e2ee964) SHA1(9464118c7cf68a16c2faaa8b8122f98105612be5) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "clubnudger4.2.bin", 0x000001, 0x008000, CRC(26f90419) SHA1(8e2884b884fc68ed15d79ac11b39d25201583687) )
	ROM_LOAD16_BYTE( "clubnudger4.3.bin", 0x010000, 0x008000, CRC(c91ca3ea) SHA1(0aa29a74a3b50c71ce37d46bf7dd6b56297e73da) )
	ROM_LOAD16_BYTE( "clubnudger4.4.bin", 0x010001, 0x008000, CRC(b9b13f25) SHA1(6a4f2d2524f2c780680b9a0c528d3603e708584f) )
ROM_END


ROM_START( j5clbnuda )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "clubnudgertest3.1.bin", 0x000000, 0x008000, CRC(365644a6) SHA1(1f0a7f92f4f0250cbe680361b88f58939580208f) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "clubnudgertest3.2.bin", 0x000001, 0x008000, CRC(c2adfb0f) SHA1(4092ebd5c8083e09c47fcefbe080caaa32083ae2) )
	ROM_LOAD16_BYTE( "clubnudgertest3.3.bin", 0x010000, 0x008000, CRC(58c3c11b) SHA1(dbd31ef33bfc78c78b7bc16a55fabe3dc9363d2c) )
	ROM_LOAD16_BYTE( "clubnudgertest3.4.bin", 0x010001, 0x008000, CRC(7f483309) SHA1(f535a64a21cae5eddc641ca880453d6616dc55c0) )
ROM_END


ROM_START( j5daytn )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "day1_300_6.b8", 0x000000, 0x010000, CRC(0e462f40) SHA1(32a2489ef0572f0119e940d85e38e75da22ceb38) )
	ROM_LOAD16_BYTE( "day2_300_6.b8", 0x000001, 0x010000, CRC(13b9e2da) SHA1(e89fa75cd1a3f391a143220b2ec52fbb95b4f6b9) )
ROM_END

ROM_START( j5daytna )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "day1_750_6.b8", 0x000000, 0x010000, CRC(796ef056) SHA1(84fde3b3c66d3ac463a5c6ab1253366bee00692b) )
	ROM_LOAD16_BYTE( "day2_750_6.b8", 0x000001, 0x010000, CRC(47823334) SHA1(fbd753e77fc96afd8dee09adc478bb285b1c4487) )
ROM_END

ROM_START( j5daycls )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "dc30cz04_1.b8", 0x000000, 0x010000, CRC(a79c1637) SHA1(bb29c63031aa0c524b089ccd2e71635de634e619) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "dc30cz04_2.b8", 0x000001, 0x010000, CRC(211c51f2) SHA1(1f608818560202b02ab169f04436232c6a1142a6) )
ROM_END

ROM_START( j5dayclsa )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "dc75cz04_1.b8", 0x000000, 0x010000, CRC(93cd54d2) SHA1(82e11a7ff0715b968e1ab5fae87b64be1efc38a5) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "dc30cz04_2.b8", 0x000001, 0x010000, CRC(211c51f2) SHA1(1f608818560202b02ab169f04436232c6a1142a6) )
ROM_END


ROM_START( j5dirty )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "ddozp1", 0x000000, 0x010000, CRC(59d66085) SHA1(a061f6f03f0320f709a92aa47f4e7624776b4866) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "ddozp2", 0x000001, 0x010000, CRC(b4b1f80c) SHA1(5314013bf21943eff5ade694832c58f378dae1f2) )
ROM_END

ROM_START( j5dirtya )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "ddz10p1", 0x000000, 0x010000, CRC(bfa18d96) SHA1(9096fe0e844e7c681058f2f60d6586d9ec8246c6) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "ddz10p2", 0x000001, 0x010000, CRC(69b5099d) SHA1(a5f1426c5d1e45c1740f6626bb48d0d8c412b0b5) )
ROM_END

ROM_START( j5dirtyb ) // set needs checking, might really use doz8cap2
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "ddoz8ss", 0x000000, 0x010000, CRC(4df255b1) SHA1(e68926e5c243d392cf2bf8091903d2b131e161d4) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "ddoz8p2", 0x000001, 0x010000, CRC(ba8e825e) SHA1(366742815682cc860a325becaaf2e4c0977fc874) )
ROM_END

ROM_START( j5dirtyc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	//ROM_LOAD16_BYTE( "doz8cap1", 0x000000, 0x010000, CRC(78da2dbc) SHA1(568462c643039939b6db19658c934fd1f54298e6) )
	ROM_LOAD16_BYTE( "doz8cap1", 0x000000, 0x010000, NO_DUMP ) // rom in set matched rom below, which can't be correct
	ROM_LOAD16_BYTE( "doz8cap2", 0x000001, 0x010000, CRC(78da2dbc) SHA1(568462c643039939b6db19658c934fd1f54298e6) )
ROM_END

// hopefully this is the right sound rom, because there was an Impact HW set in here too
#define J5FAIRGD_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "cfg_snd", 0x000000, 0x080000, CRC(57ea2159) SHA1(79eb864333ecdfaacae51797327afe5cc8a815eb) ) \

ROM_START( j5fairgd )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6474.bin", 0x000000, 0x010000, CRC(e02bea2d) SHA1(b93f44b04f64ffd19952447889f21ec4e43eef0f) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6475.bin", 0x000001, 0x010000, CRC(bdca4770) SHA1(5dde691a6275531132228f1c6bb884d345e8a3de) )
	J5FAIRGD_SOUND
ROM_END

ROM_START( j5fairgda )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6476.bin", 0x000000, 0x010000, CRC(17bbc87f) SHA1(4c7ac3c1efe19313fcc2d3e0ff1d978066c66171) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6475.bin", 0x000001, 0x010000, CRC(bdca4770) SHA1(5dde691a6275531132228f1c6bb884d345e8a3de) )
	J5FAIRGD_SOUND
ROM_END

ROM_START( j5fairgdb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6477.bin", 0x000000, 0x010000, CRC(f7ad4c88) SHA1(487acae3f9f7cace8c0cd5f9c1cc35e758ac0ae8) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6478.bin", 0x000001, 0x010000, CRC(bc27e344) SHA1(bcfd0cd04ada31a037b9f5dcd7e47278596b18b5) )
	J5FAIRGD_SOUND
ROM_END

ROM_START( j5fairgdc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6479.bin", 0x000000, 0x010000, CRC(003d6eda) SHA1(324e2fe867c4944534075cc6442f5a2d09efd443) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6478.bin", 0x000001, 0x010000, CRC(bc27e344) SHA1(bcfd0cd04ada31a037b9f5dcd7e47278596b18b5) )
	J5FAIRGD_SOUND
ROM_END

ROM_START( j5fairgdd )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "fairground_attraction_4_1.bin", 0x000000, 0x010000, CRC(5ade1db9) SHA1(b45e946b9c54de656006597ea72dab97e2bdbd3a) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "fairground_attraction_4_2.bin", 0x000001, 0x010000, CRC(c9d7817f) SHA1(893a5be457f19d991d5ee8af4c6e27331905e17c) )
	J5FAIRGD_SOUND
ROM_END

ROM_START( j5fairgde )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "fairground_attraction_5_1.bin", 0x000000, 0x010000, CRC(314ecf4d) SHA1(bb5a07987b151372f91a3458ab906321b398008a) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "fairground_attraction_5_2.bin", 0x000001, 0x010000, CRC(7b160beb) SHA1(8f96e41f72f937bf6feef48d37e400a4dcfecd84) )
	J5FAIRGD_SOUND
ROM_END

#define J5FAIR_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "fairsound.bin", 0x0000, 0x040000, CRC(2992a89a) SHA1(74b972a234c96217c8ebd0e724e97dbb5afe6fc1) )\

ROM_START( j5fair )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6248.bin", 0x00000, 0x010000, CRC(96b3fbc9) SHA1(4203a70ba444caba4496ced4168f271a7e405568) )
	//ROM_LOAD16_BYTE( "fairground 6 6-1.bin", 0x0000, 0x010000, CRC(96b3fbc9) SHA1(4203a70ba444caba4496ced4168f271a7e405568) )
	ROM_LOAD16_BYTE( "6249.bin", 0x00001, 0x010000, CRC(cc8325db) SHA1(0cbfc9157af7f8a25fbf81d28354d98f829455c0) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5faira )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6250.bin", 0x00000, 0x010000, CRC(6123d99b) SHA1(4725a18c0649fb1ba53683d0cbaf9b7fe81204e8) )
	ROM_LOAD16_BYTE( "6249.bin", 0x00001, 0x010000, CRC(cc8325db) SHA1(0cbfc9157af7f8a25fbf81d28354d98f829455c0) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6310.bin", 0x00000, 0x010000, CRC(aa2eb82c) SHA1(522d733cfd3f8e1aaf596acde29ba2152ec03e88) )
	ROM_LOAD16_BYTE( "6249.bin", 0x00001, 0x010000, CRC(cc8325db) SHA1(0cbfc9157af7f8a25fbf81d28354d98f829455c0) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6311.bin", 0x00000, 0x010000, CRC(a2e2b92c) SHA1(affe5ac7b6895b73d28be139557fcd27c4e81997) )
	ROM_LOAD16_BYTE( "6249.bin", 0x00001, 0x010000, CRC(cc8325db) SHA1(0cbfc9157af7f8a25fbf81d28354d98f829455c0) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5faird )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6781.bin", 0x00000, 0x010000, CRC(5dbe9a7e) SHA1(8434fe268b7f44b73df344d25ced9768e90faae3) )
	ROM_LOAD16_BYTE( "6249.bin", 0x00001, 0x010000, CRC(cc8325db) SHA1(0cbfc9157af7f8a25fbf81d28354d98f829455c0) )

	J5FAIR_SOUND
ROM_END


ROM_START( j5faire )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8510.bin", 0x00000, 0x010000, CRC(1f05a0ec) SHA1(5d6ff1d2b2ec73bfbe7a2ff17e0c95ebada82d81) )
	ROM_LOAD16_BYTE( "8511.bin", 0x00001, 0x010000, CRC(189f9c43) SHA1(ad54234a953d0bde4a6ee1fb1ebac2d49307d115) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairf )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8512.bin", 0x00000, 0x010000, CRC(e89582be) SHA1(83e280dd1e747c50ce45ca502893b45406bc2a1f) )
	ROM_LOAD16_BYTE( "8511.bin", 0x00001, 0x010000, CRC(189f9c43) SHA1(ad54234a953d0bde4a6ee1fb1ebac2d49307d115) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairg )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8513.bin", 0x00000, 0x010000, CRC(2398e309) SHA1(0e4aea0c93a5fc6ff81f639b494645f3a4330b5f) )
	ROM_LOAD16_BYTE( "8511.bin", 0x00001, 0x010000, CRC(189f9c43) SHA1(ad54234a953d0bde4a6ee1fb1ebac2d49307d115) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairh )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8514.bin", 0x00000, 0x010000, CRC(2b54e209) SHA1(7bf0ebd390b0f360e8777bc3bf88c6d3f22c90cd) )
	ROM_LOAD16_BYTE( "8511.bin", 0x00001, 0x010000, CRC(189f9c43) SHA1(ad54234a953d0bde4a6ee1fb1ebac2d49307d115) )

	J5FAIR_SOUND
ROM_END


ROM_START( j5fairi )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8555.bin", 0x00000, 0x010000, CRC(97379eea) SHA1(98298acfe38811746d0a28dda36c1f9b542a3f08) )
	ROM_LOAD16_BYTE( "8556.bin", 0x00001, 0x010000, CRC(6b143dd1) SHA1(c1cc3299c0e1ebb4ad2d161020dbb646776c24fb) )

	J5FAIR_SOUND
ROM_END


ROM_START( j5fairj )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8557.bin", 0x00000, 0x010000, CRC(60a7bcb8) SHA1(60909e13dc0e0c6fa3db3b11bc4bf221bee50749) )
	ROM_LOAD16_BYTE( "8556.bin", 0x00001, 0x010000, CRC(6b143dd1) SHA1(c1cc3299c0e1ebb4ad2d161020dbb646776c24fb) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairk )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8558.bin", 0x00000, 0x010000, CRC(abaadd0f) SHA1(bbf0e1d07a3f6a35349f102c1a920fcf7805fd3c) )
	ROM_LOAD16_BYTE( "8556.bin", 0x00001, 0x010000, CRC(6b143dd1) SHA1(c1cc3299c0e1ebb4ad2d161020dbb646776c24fb) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairl )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "8559.bin", 0x00000, 0x010000, CRC(a366dc0f) SHA1(4769d5ad9a91f1b05702a19857f7d0dca884ceeb) )
	ROM_LOAD16_BYTE( "8556.bin", 0x00001, 0x010000, CRC(6b143dd1) SHA1(c1cc3299c0e1ebb4ad2d161020dbb646776c24fb) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairm )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "fairground_10_1.bin", 0x00000, 0x010000, CRC(aa423087) SHA1(060cce5d69e4d191fd7ee711c834713abb17dc0d) )
	ROM_LOAD16_BYTE( "fairground_10_2.bin", 0x00001, 0x010000, CRC(8a68ea25) SHA1(d7ce2bad02cfca20a936ce4f987f124e80c8c210) )
	//ROM_LOAD( "fairground_10_quid_p1.bin", 0x0000, 0x010000, CRC(aa423087) SHA1(060cce5d69e4d191fd7ee711c834713abb17dc0d) )
	//ROM_LOAD( "fairground_10_quid_p2.bin", 0x0000, 0x010000, CRC(8a68ea25) SHA1(d7ce2bad02cfca20a936ce4f987f124e80c8c210) )
	//ROM_LOAD( "fairground_20p_25p_10_p1.bin", 0x0000, 0x010000, CRC(aa423087) SHA1(060cce5d69e4d191fd7ee711c834713abb17dc0d) )
	//ROM_LOAD( "fairground_20p_25p_10_p2.bin", 0x0000, 0x010000, CRC(8a68ea25) SHA1(d7ce2bad02cfca20a936ce4f987f124e80c8c210) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairn )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "fairgp1", 0x00000, 0x010000, CRC(b50afc36) SHA1(f8ce7bbecb7faaccc6f9350a6a414f931e456a0d) )
	ROM_LOAD16_BYTE( "fairgp2", 0x00001, 0x010000, CRC(dab2b7a1) SHA1(8ec12ebc40768eb43aaeeb0def6dfb532633abec) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairo )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "fairground8cashp1.bin", 0x00000, 0x010000, CRC(815bbed3) SHA1(f7e94c8ec2219c62d6e732b9eaf20629ca5d4db2) )
	ROM_LOAD16_BYTE( "fairground8cashp2.bin", 0x00001, 0x010000, CRC(dab2b7a1) SHA1(8ec12ebc40768eb43aaeeb0def6dfb532633abec) ) // == fairgp2

	J5FAIR_SOUND
ROM_END


ROM_START( j5fairp )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "fairground_8_1.bin", 0x00000, 0x010000, CRC(a9a25c65) SHA1(dd8f23637baf3793225a544ebbe3deb0ef21130d) )
	ROM_LOAD16_BYTE( "fairground_8_2.bin", 0x00001, 0x010000, CRC(7333c1ec) SHA1(a28653bff9e6887f47b030951922797f21c02ccc) )

	J5FAIR_SOUND
ROM_END

ROM_START( j5fairq )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "fgrndp1", 0x00000, 0x010000, CRC(a8b30640) SHA1(614b10335268df7f353674bb7111e9e8c762084c) )
	ROM_LOAD16_BYTE( "fgrndp2", 0x00001, 0x010000, CRC(250c3fd0) SHA1(75f85944683d72b93ee6876db42df6ad9c0d4f61) )
	//ROM_LOAD( "fairp2ca", 0x0000, 0x010000, CRC(250c3fd0) SHA1(75f85944683d72b93ee6876db42df6ad9c0d4f61) )

	J5FAIR_SOUND
ROM_END







//  these are from FAST?TRAK on Impact HW!!! (found in Fairground set)
//  ROM_LOAD16_BYTE( "9331.bin", 0x000000, 0x020000, CRC(54dbf894) SHA1(a3ffff82883cc192108f44d36a7465d4afeaf114) )
//  ROM_LOAD16_BYTE( "9332.bin", 0x000001, 0x020000, CRC(ecf1632a) SHA1(5d82a46672adceb29744e82de1b0fa5fcf4dbc51) )
//  ROM_LOAD16_BYTE( "9333.bin", 0x000000, 0x020000, CRC(bf45acac) SHA1(ec624bc2d135901ecbdb6c6b3dbd9cc4b618b4de) )
//  ROM_LOAD16_BYTE( "9334.bin", 0x000000, 0x020000, CRC(061f38f5) SHA1(459b39d2380fcfdb763eeb6937752be192cb8244) )
//  ROM_LOAD16_BYTE( "9335.bin", 0x000000, 0x020000, CRC(36b6891c) SHA1(013b663f2dc59a4d2834ef2f7e86bcc608e98b39) )


// Not sure which is the right sound rom, there was an Impact HW set in here too
#define J5JOKGLD_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "jg_snd.bin", 0x000000, 0x080000, CRC(bab05fea) SHA1(66e03ac598f6683b6634a2fce194dc058ddc8ef4) ) \
	ROM_LOAD( "jgsnd.bin",  0x000000, 0x080000, CRC(8bc92c90) SHA1(bcbbe270ce42d5960ac37a2324e3fb37ff513147) ) \

// Also with these roms: German Impact HW set?
//  ROM_LOAD16_BYTE( "jg.p1", 0x00000, 0x010000, CRC(e5658ca2) SHA1(2d188899a4aa8124b7c492379331b8713913c69e) )
//  ROM_LOAD16_BYTE( "jg.p2", 0x00001, 0x010000, CRC(efa0c84b) SHA1(ef511378904823ae66b7812eff13d9cef5fa621b) )

ROM_START( j5jokgld )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jg30cz03_1.b8", 0x00000, 0x010000, CRC(106a1ee6) SHA1(967abfa3bf90566ebfbe0be81b3ca41bba6bd858) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "jg30cz03_2.b8", 0x00001, 0x010000, CRC(e883436c) SHA1(2425663a336284816a030de8d2b0765f47fed213) )
	J5JOKGLD_SOUND
ROM_END

ROM_START( j5jokglda )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jg75cz03_1.b8", 0x000000, 0x010000, CRC(243b5c03) SHA1(86dc3a95718b3bcea580ae985bf50d944242857b) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "jg30cz03_2.b8", 0x000001, 0x010000, CRC(e883436c) SHA1(2425663a336284816a030de8d2b0765f47fed213) )
	J5JOKGLD_SOUND
ROM_END

ROM_START( j5jokgldb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jg30cz04_1.b8", 0x000000, 0x010000, CRC(95bb355f) SHA1(74f245abb079c4a19422b56954a56702b40b2973) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "jg30cz04_2.b8", 0x000001, 0x010000, CRC(be6cf84b) SHA1(8b9c504c27654ef4f4cb5124ed53a6f3ebc6c389) )
	J5JOKGLD_SOUND
ROM_END

ROM_START( j5jokgldc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jg75cz04_1.b8", 0x000000, 0x010000, CRC(a1ea77ba) SHA1(a71269117dfcee67117df54682fda3424ae555be) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "jg30cz04_2.b8", 0x000001, 0x010000, CRC(be6cf84b) SHA1(8b9c504c27654ef4f4cb5124ed53a6f3ebc6c389) )
	J5JOKGLD_SOUND
ROM_END

ROM_START( j5jokgldd )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jg30cz04_1a.b8", 0x000000, 0x010000, CRC(00f6a5d4) SHA1(698d1dca3e70de515a7c4139dc3f8a98ec1a863a) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "jg30cz04_2a.b8", 0x000001, 0x010000, CRC(12daedf2) SHA1(6987660fdb63b8d56df14fc620de61cf29638d4a) )
	J5JOKGLD_SOUND
ROM_END

ROM_START( j5jokglde )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jg75cz04_1a.b8", 0x000000, 0x010000, CRC(34a7e731) SHA1(c936ca22eaeb149c97dac0c21447ad00dca0cadd) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "jg30cz04_2a.b8", 0x000001, 0x010000, CRC(12daedf2) SHA1(6987660fdb63b8d56df14fc620de61cf29638d4a) )
	J5JOKGLD_SOUND
ROM_END

ROM_START( j5jokgldf ) // check this set
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jgcz30_5_1.b8", 0x000000, 0x010000, CRC(3f34bda5) SHA1(a07291e0b657833cc0cbe49d0d9b6640bfdd6d60) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "jgcz_5_2.b8",   0x000001, 0x010000, CRC(11e59b70) SHA1(00c17ca4f0f6bb3312f19900401ea52688574986) )
	J5JOKGLD_SOUND
ROM_END

ROM_START( j5jokgldg ) // check this set
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jgcz75_5_1.b8", 0x000000, 0x010000, CRC(0b65ff40) SHA1(2fe52d05a9869b124e5ada5051bfa90139808f85) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "jgcz_5_2.b8",   0x000001, 0x010000, CRC(11e59b70) SHA1(00c17ca4f0f6bb3312f19900401ea52688574986) )
	J5JOKGLD_SOUND
ROM_END

ROM_START( j5jokgldh ) // check this set
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "jg75cz05_1.b8", 0x000000, 0x010000, CRC(becdf295) SHA1(c80e547afdbf3f54835489e84a3f16dd8398cf47) )
	ROM_LOAD16_BYTE( "jgcz05_2.b8",   0x000001, 0x010000, CRC(9401c284) SHA1(eb4992fdfbc3266a0102769a24733fa26de828d9) )
	J5JOKGLD_SOUND
ROM_END






ROM_START( j5filth )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6575.bin", 0x000000, 0x010000, CRC(25143b59) SHA1(9203c13bd21f964ba868b2e094b166ee20d52041) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6576.bin", 0x000001, 0x010000, CRC(d0482a59) SHA1(5dff247f66d8f6bbdcebfafe5d754b7dd6fd6790) )
ROM_END

ROM_START( j5filtha )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6577.bin", 0x000000, 0x010000, CRC(d284190b) SHA1(53cf4e4191f7c3e35b3e2871c0434f2890aad6db) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6576.bin", 0x000001, 0x010000, CRC(d0482a59) SHA1(5dff247f66d8f6bbdcebfafe5d754b7dd6fd6790) )
ROM_END

ROM_START( j5filthb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6617.bin", 0x000000, 0x010000, CRC(114579bc) SHA1(33ec391c33abae3976dd2263b0b57b25185995d3) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "6576.bin", 0x000001, 0x010000, CRC(d0482a59) SHA1(5dff247f66d8f6bbdcebfafe5d754b7dd6fd6790) )
ROM_END


ROM_START( j5filthc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7174.bin", 0x000000, 0x010000, CRC(fff88f1d) SHA1(f0689b844b001b8ccca1199178ff412c0f15efa9) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "7175.bin", 0x000001, 0x010000, CRC(d0492efc) SHA1(ef6cc27031db93a64dafa1cf3ce3fe5836fee1c4) )
ROM_END

ROM_START( j5filthd )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7176.bin", 0x000000, 0x010000, CRC(0868ad4f) SHA1(2cc6dffd545a3f5a39d5d12b9f94423d18f64f80) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "7175.bin", 0x000001, 0x010000, CRC(d0492efc) SHA1(ef6cc27031db93a64dafa1cf3ce3fe5836fee1c4) )
ROM_END



ROM_START( j5filthe )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7396.bin", 0x000000, 0x010000, CRC(cba9cdf8) SHA1(ead3a5f5c4ff99b9c71e8521442ba0d6d2ca92d6) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "7175.bin", 0x000001, 0x010000, CRC(d0492efc) SHA1(ef6cc27031db93a64dafa1cf3ce3fe5836fee1c4) )
ROM_END


ROM_START( j5filthf )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7180.bin", 0x000000, 0x010000, CRC(8d7bddc7) SHA1(e2f3001e3e01fa1d0c8b3a9529f6cc0cf2902a4d) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "7181.bin", 0x000000, 0x010000, CRC(8e87eed4) SHA1(6776fe1b79a4607b7d848f6cfbbeac2aaf126a1b) )
ROM_END


ROM_START( j5filthg )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7182.bin", 0x000000, 0x010000, CRC(7aebff95) SHA1(bad1036590a566537c58a963a1e4f41a334a9481) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "7181.bin", 0x000000, 0x010000, CRC(8e87eed4) SHA1(6776fe1b79a4607b7d848f6cfbbeac2aaf126a1b) )
ROM_END


ROM_START( j5filthh )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7501.bin", 0x000000, 0x010000, CRC(a984e6e9) SHA1(47a6625e7944cf1579227d3f6da03f9f54bd80ef) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "7502.bin", 0x000000, 0x010000, CRC(31837e3d) SHA1(0ebd49a5dbc4fc7f8272d471b135f62d050d51c4) )
ROM_END


ROM_START( j5filthi )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7503.bin", 0x000000, 0x010000, CRC(5e14c4bb) SHA1(fd5570448cba167d7d6cdc41f132f72546d81458) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "7502.bin", 0x000000, 0x010000, CRC(31837e3d) SHA1(0ebd49a5dbc4fc7f8272d471b135f62d050d51c4) )
ROM_END

ROM_START( j5filthj )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7504.bin", 0x000000, 0x010000, CRC(9519a50c) SHA1(a5828f9b03eda74befe39b60fe8f82226b253d8b) )  // 0x81 = BF
	ROM_LOAD16_BYTE( "7502.bin", 0x000000, 0x010000, CRC(31837e3d) SHA1(0ebd49a5dbc4fc7f8272d471b135f62d050d51c4) )
ROM_END



ROM_START( j5firebl )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "4711.bin", 0x000000, 0x008000, CRC(578f1bfe) SHA1(8d6bd79cc50108c01d26f8a93e990a2d1a509cf1) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "4712.bin", 0x000001, 0x008000, CRC(2031c757) SHA1(98df775e55ad99e8ae6031085686574a8b4b633f) )
	ROM_LOAD16_BYTE( "4713.bin", 0x010000, 0x008000, CRC(c74a41e5) SHA1(67f004ba5d3885feb6451a25b961fdae0bcf132b) )
	ROM_LOAD16_BYTE( "4714.bin", 0x010001, 0x008000, CRC(bcee0397) SHA1(e5c463b7cda1871fef41c191f8de32de1c7a4655) )
ROM_END

ROM_START( j5firebla )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "4715.bin", 0x000000, 0x008000, CRC(645162df) SHA1(41cc845d74f5dab36c3cb53b62aa13799b8ad61f) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "4712.bin", 0x000001, 0x008000, CRC(2031c757) SHA1(98df775e55ad99e8ae6031085686574a8b4b633f) )
	ROM_LOAD16_BYTE( "4713.bin", 0x010000, 0x008000, CRC(c74a41e5) SHA1(67f004ba5d3885feb6451a25b961fdae0bcf132b) )
	ROM_LOAD16_BYTE( "4714.bin", 0x010001, 0x008000, CRC(bcee0397) SHA1(e5c463b7cda1871fef41c191f8de32de1c7a4655) )
ROM_END

ROM_START( j5fireblb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "4213.bin", 0x000000, 0x008000, CRC(12183516) SHA1(7f2485ad1e15655c45f74d0ea946a30cbf0d1969) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "4210.bin", 0x000001, 0x008000, CRC(112449c5) SHA1(bedbd7f53e4e198764919170dadc7308406cc4ea) )
	ROM_LOAD16_BYTE( "4211.bin", 0x010000, 0x008000, CRC(f1831095) SHA1(1143f863ce4259add3d08c5879053da369e8269f) )
	ROM_LOAD16_BYTE( "4212.bin", 0x010001, 0x008000, CRC(b5aff718) SHA1(cfc84d0bb7cad20c00242eb29f36af6467978036) )
ROM_END


ROM_START( j5frmag )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "frm1.bin", 0x000000, 0x008000, CRC(0c54ff1d) SHA1(68e08abdb61520189903071cfcd7d8677f7fdff2) )
	ROM_LOAD16_BYTE( "frm2.bin", 0x000001, 0x008000, CRC(eeb26fb2) SHA1(788b4bee08fef5b41fba73e1aaf38fd4b804ad4d) )
	ROM_LOAD16_BYTE( "frm3.bin", 0x010000, 0x008000, CRC(ae194067) SHA1(bc41e9533a74e2c83f0773efbd038b44a55a0bdf) )
	ROM_LOAD16_BYTE( "frm4.bin", 0x010001, 0x008000, CRC(9afb4ed9) SHA1(34ae4147d5c20274887beb694d823b8362f6d157) )
ROM_END



ROM_START( j5hagsho )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6440.bin", 0x000000, 0x010000, CRC(7cbed823) SHA1(e7361eda5545ae7a8d99bc9ca45737144f303e14) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6441.bin", 0x000001, 0x010000, CRC(84c12ded) SHA1(1e199a90214ae78f4e03ddc088ff7c2c19c259ed) )
ROM_END

ROM_START( j5hagshoa )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6442.bin", 0x000000, 0x010000, CRC(8b2efa71) SHA1(c377e53bee2d05e25f6abe5298e78422227d2e5b) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6441.bin", 0x000001, 0x010000, CRC(84c12ded) SHA1(1e199a90214ae78f4e03ddc088ff7c2c19c259ed) )
ROM_END

ROM_START( j5hagshob )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6443.bin", 0x000000, 0x010000, CRC(40239bc6) SHA1(c1793b8cb3ea38c03f9dadc033f39c97d0c9534c) ) // 0x81 = BF
	ROM_LOAD16_BYTE( "6441.bin", 0x000001, 0x010000, CRC(84c12ded) SHA1(1e199a90214ae78f4e03ddc088ff7c2c19c259ed) )
ROM_END

ROM_START( j5hagshoc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6444.bin", 0x000000, 0x010000, CRC(48ef9ac6) SHA1(d6970d1837a6b354ca3eed0ac43ce569214f4916) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "6441.bin", 0x000001, 0x010000, CRC(84c12ded) SHA1(1e199a90214ae78f4e03ddc088ff7c2c19c259ed) )
ROM_END



ROM_START( j5holly )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5042.bin", 0x000000, 0x008000, CRC(f3dfd249) SHA1(6db5e1d02e33b5bc8922c47e5f16438583c8b416) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "5043.bin", 0x000001, 0x008000, CRC(4109c681) SHA1(43736cbcd843104c08d97e20cdf4cc587fbd76ee) )
	ROM_LOAD16_BYTE( "5044.bin", 0x010000, 0x008000, CRC(867a94c5) SHA1(3f6a46b0c1cc1b796f26cfddd35a8ac206f39b38) )
	ROM_LOAD16_BYTE( "5045.bin", 0x010001, 0x008000, CRC(e280c98e) SHA1(a922cb1402a81366760490ee2724059081c3b5e5) )
ROM_END

ROM_START( j5hollya )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5046.bin", 0x000000, 0x008000, CRC(c001ab68) SHA1(2bda4c41334aa21893ec5517d8dd343ea3d111b7) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "5043.bin", 0x000001, 0x008000, CRC(4109c681) SHA1(43736cbcd843104c08d97e20cdf4cc587fbd76ee) )
	ROM_LOAD16_BYTE( "5044.bin", 0x010000, 0x008000, CRC(867a94c5) SHA1(3f6a46b0c1cc1b796f26cfddd35a8ac206f39b38) )
	ROM_LOAD16_BYTE( "5045.bin", 0x010001, 0x008000, CRC(e280c98e) SHA1(a922cb1402a81366760490ee2724059081c3b5e5) )
ROM_END

ROM_START( j5hollyb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5047.bin", 0x000000, 0x008000, CRC(9463200b) SHA1(69bf2d52f6a859334ff4a2615f0c165df26e0efa) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "5043.bin", 0x000001, 0x008000, CRC(4109c681) SHA1(43736cbcd843104c08d97e20cdf4cc587fbd76ee) )
	ROM_LOAD16_BYTE( "5044.bin", 0x010000, 0x008000, CRC(867a94c5) SHA1(3f6a46b0c1cc1b796f26cfddd35a8ac206f39b38) )
	ROM_LOAD16_BYTE( "5045.bin", 0x010001, 0x008000, CRC(e280c98e) SHA1(a922cb1402a81366760490ee2724059081c3b5e5) )
ROM_END

ROM_START( j5hollyc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5048.bin", 0x000000, 0x008000, CRC(a9a3b6a9) SHA1(93262c1284b7907dd3f13645882a036c981f8701) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "5049.bin", 0x000001, 0x008000, CRC(478da1d4) SHA1(2c9194b4db85de94674d3646564d15d52620be47) )
	ROM_LOAD16_BYTE( "5050.bin", 0x010000, 0x008000, CRC(ffc6c8d1) SHA1(5f2509f36aeed3e8635b2279be43b3b0a002876d) )
	ROM_LOAD16_BYTE( "5051.bin", 0x010001, 0x008000, CRC(adf88ef9) SHA1(cdb82e4b5f88116e3e7f9dda136426351c5450dd) )
ROM_END

ROM_START( j5hollyd )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5052.bin", 0x000000, 0x008000, CRC(9a7dcf88) SHA1(fdb8c50f2809eb8caee22167c9874e6c0afb5ece) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "5049.bin", 0x000001, 0x008000, CRC(478da1d4) SHA1(2c9194b4db85de94674d3646564d15d52620be47) )
	ROM_LOAD16_BYTE( "5050.bin", 0x010000, 0x008000, CRC(ffc6c8d1) SHA1(5f2509f36aeed3e8635b2279be43b3b0a002876d) )
	ROM_LOAD16_BYTE( "5051.bin", 0x010001, 0x008000, CRC(adf88ef9) SHA1(cdb82e4b5f88116e3e7f9dda136426351c5450dd) )
ROM_END

ROM_START( j5hollye )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "holly6p1", 0x000000, 0x008000, CRC(36d3e8f7) SHA1(3f641e09442b6048b0ce8c4f11a764cc6e292464) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "holly6p2", 0x000001, 0x008000, CRC(61ee2f90) SHA1(bfc021296408f7bf843653a55e0e9ca3860c274c) )
	ROM_LOAD16_BYTE( "holly6p3", 0x010000, 0x008000, CRC(291d0931) SHA1(b94a2b3db07d5cc9fe03368fd8d4e269f6d67802) )
	ROM_LOAD16_BYTE( "holly6p4", 0x010001, 0x008000, CRC(84ff7442) SHA1(0b047d68de98acb8632dcd8a243826dfabad8ecb) )
ROM_END

#define J5HOTDOG_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD16_BYTE( "hot_dogs_snd.bin", 0x000000, 0x040000, CRC(cd7eae1c) SHA1(48b6344491bf0f40e02fbdec5a26f546f2b8d7bb) ) \


ROM_START( j5hotdog )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "hdog5pp1", 0x000000, 0x010000, CRC(ef227a6b) SHA1(a7033faff1868cbafa41281c77d0ee0efd824529) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "hdog5pp2", 0x000001, 0x010000, CRC(65c5ce4b) SHA1(ed96803f7b49dbbb4c7949b574a38cc7b1e34536) )
	J5HOTDOG_SOUND
ROM_END

ROM_START( j5hotdoga )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "hdogp1", 0x000000, 0x010000, CRC(a510d294) SHA1(d7ccb54c91e844d81d1dacc8ffd38c03d8ab1be7) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "hdogp2", 0x000001, 0x010000, CRC(94b58a0f) SHA1(891db9e7a1944751da9aac55f7b3c1a3652bdecb) )
	J5HOTDOG_SOUND
ROM_END



ROM_START( j5indsum )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5668.bin", 0x000000, 0x008000, CRC(33acd940) SHA1(9002c3d451ed06052cae7ba9c71f55b84bff42ee) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "5669.bin", 0x000001, 0x008000, CRC(0bb56700) SHA1(0cf685ab43ca5d0e23af76e3c857dea8aa848454) )
	ROM_LOAD16_BYTE( "5670.bin", 0x010000, 0x008000, CRC(a4e883cd) SHA1(7bf847cc4a3323b501931e8ed0687aab33da817c) )
	ROM_LOAD16_BYTE( "5671.bin", 0x010001, 0x008000, CRC(858c6a2c) SHA1(491ce4390d416e4ade4ae6661be0524f3e71af6b) )
ROM_END


ROM_START( j5indy )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "indy04020.p1", 0x000000, 0x010000, CRC(52ca267c) SHA1(7cd90b536d974afd504620f12868c5efe83ef9dd) )
	ROM_LOAD16_BYTE( "indy04020.p2", 0x000001, 0x010000, CRC(d96e3937) SHA1(902bb6021f5e1fa7471bce9de207e4df5f071fc4) )

	ROM_REGION( 0x80000, "upd7759", 0 )
	ROM_LOAD( "indy04020.snd", 0x000000, 0x080000, CRC(0266d48e) SHA1(c16ebdc6f73a6c832f765b909ca5511eaf728897) )
ROM_END


ROM_START( j5intr )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5747.bin", 0x000000, 0x008000, CRC(7f4240c8) SHA1(066fc2c3393b2c863cacef2dec5aa1dc87f6e428) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "5748.bin", 0x000001, 0x008000, CRC(87349748) SHA1(6c432628a94fb50a7e09d90f6ba556cc68a56ab3) )
	ROM_LOAD16_BYTE( "5749.bin", 0x010000, 0x008000, CRC(335e7d65) SHA1(0a45bb4d9c60a57a9e0c70e4b5d3d3f82e8c19c0) )
	ROM_LOAD16_BYTE( "5750.bin", 0x010001, 0x008000, CRC(ee4b0133) SHA1(f1bdc038a7505dc1c88ec16f3bacbad63fe8e827) )
ROM_END

ROM_START( j5intra )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5751.bin", 0x000000, 0x008000, CRC(4c9c39e9) SHA1(8143eae028629f8dc05274fba46af92b5113c64f) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "5748.bin", 0x000001, 0x008000, CRC(87349748) SHA1(6c432628a94fb50a7e09d90f6ba556cc68a56ab3) )
	ROM_LOAD16_BYTE( "5749.bin", 0x010000, 0x008000, CRC(335e7d65) SHA1(0a45bb4d9c60a57a9e0c70e4b5d3d3f82e8c19c0) )
	ROM_LOAD16_BYTE( "5750.bin", 0x010001, 0x008000, CRC(ee4b0133) SHA1(f1bdc038a7505dc1c88ec16f3bacbad63fe8e827) )
ROM_END

ROM_START( j5intrb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6022.bin", 0x000000, 0x008000, CRC(59e710c8) SHA1(1a0c98f7a1553c5cda342b4d3c0da9a0a734a5e0) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6023.bin", 0x000001, 0x008000, CRC(17a0936e) SHA1(4aabbdfe4cf5d1ab75c3477e9f459cece56b936c) )
	ROM_LOAD16_BYTE( "6024.bin", 0x010000, 0x008000, CRC(4656900c) SHA1(83e0fc8fae8308e30a5c88ad9dfb9b376ce7da4c) )
	ROM_LOAD16_BYTE( "6025.bin", 0x010001, 0x008000, CRC(1f7c315a) SHA1(ef5e328e01011753ecaa6d62121ec960a759762d) )
ROM_END

ROM_START( j5intrc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6026.bin", 0x000000, 0x008000, CRC(6a3969e9) SHA1(7dc4203e40309af3edfb8f7a0f6533ae5f9c4f79) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6023.bin", 0x000001, 0x008000, CRC(17a0936e) SHA1(4aabbdfe4cf5d1ab75c3477e9f459cece56b936c) )
	ROM_LOAD16_BYTE( "6024.bin", 0x010000, 0x008000, CRC(4656900c) SHA1(83e0fc8fae8308e30a5c88ad9dfb9b376ce7da4c) )
	ROM_LOAD16_BYTE( "6025.bin", 0x010001, 0x008000, CRC(1f7c315a) SHA1(ef5e328e01011753ecaa6d62121ec960a759762d) )
ROM_END



ROM_START( j5nite )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "nite_club_3_1.bin", 0x000000, 0x008000, CRC(96f8f664) SHA1(d5e3cb27dc0ead15cef0200a6f14fdaa4e6d20f1) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "nite_club_3_2.bin", 0x000001, 0x008000, CRC(56db2aed) SHA1(245406aebe597a3996a9d9630bdaa006663f9ca2) )
	ROM_LOAD16_BYTE( "nite_club_3_3.bin", 0x010000, 0x008000, CRC(e4729a99) SHA1(bdc8f739215e7407a522f18b5699411af0e45ba8) )
	ROM_LOAD16_BYTE( "nite_club_3_4.bin", 0x010001, 0x008000, CRC(397b45f5) SHA1(65526eea629f02613d14b77aa430faccedfb119c) )
ROM_END

ROM_START( j5nitea )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "nite_club_8_1.bin",  0x000000, 0x008000, CRC(012c2384) SHA1(47254832e650bc73d0c01137ea9ade0bcf83660d) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "nite_club_8-2.bin",  0x000001, 0x008000, CRC(09fe54f5) SHA1(aa5770d5fbf84b30b9e43a1407c7c0a532163757) )
	ROM_LOAD16_BYTE( "nite_club_8_3.bin",  0x010000, 0x008000, CRC(08f23a8c) SHA1(dfee1f2734dab36932a3d7343ea24064cfffab50) )
	ROM_LOAD16_BYTE( "nite_club_8_4a.bin", 0x010001, 0x008000, CRC(01d4158a) SHA1(f3748adfe3fc7e5b427593a5380b67f85c821669) )
//  ROM_LOAD16_BYTE( "nite_club_8_4.bin", 0x000000, 0x008000, CRC(3a9de9f4) SHA1(7163a31aa2b9f8027199210885b47ad87d21a8c9) ) // clearly just a bad dump, random data missing, 2nd half partly mirrors first
ROM_END



ROM_START( j5palm )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "c90 4af9 5.1.bin", 0x000000, 0x008000, CRC(4992bcc0) SHA1(151c33f303f749d0e436e8ba1cbe197109cdd4e4) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "c90 3260 5.2.bin", 0x000001, 0x008000, CRC(9744686c) SHA1(79744d7b76de8d4c551de06d450af4e04e4ee5e3) )
	ROM_LOAD16_BYTE( "c90 6c77 5.3.bin", 0x010000, 0x008000, CRC(22629877) SHA1(a6542c0851de9c1f14e4e5f0ea6e9550c2b92294) )
	ROM_LOAD16_BYTE( "c90 2f70 5.4.bin", 0x010001, 0x008000, CRC(80da5d39) SHA1(f87742c68f0ea8aaece73da9e3130bf764cdd594) )
ROM_END


ROM_START( j5palma )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "palm_springs_5_1.bin", 0x000000, 0x008000, CRC(66be0f2f) SHA1(7cb5e4e32725210fb3418f4da4d784e261e38f9d) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "palm_springs_5_2.bin", 0x000001, 0x008000, CRC(6b2071fb) SHA1(dd84eb28e48af623f38ab8fb15fb2d01a2f83036) )
	ROM_LOAD16_BYTE( "palm_springs_5_3.bin", 0x010000, 0x008000, CRC(d71d4023) SHA1(e45038d26dc88eb5cd7ca6c0ad7979b9e49b0d0c) )
	ROM_LOAD16_BYTE( "palm_springs_5_4.bin", 0x010001, 0x008000, CRC(2d33a566) SHA1(a9a9b4d1ba96448634193c6ffd4a4dac3ef95d5e) )
ROM_END



ROM_START( j5phnx )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "ph1300_8.b8", 0x000000, 0x010000, CRC(954301e3) SHA1(d71ab2c971e15dd1b5f1f64e02adb9f901365b09) )
	ROM_LOAD16_BYTE( "ph2300_8.b8", 0x000001, 0x010000, CRC(8dfd4471) SHA1(37931821acda79543b4a7e346a945180f801fb14) )
ROM_END

ROM_START( j5phnxa )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "ph1750_10.b8", 0x000000, 0x010000, CRC(a2245a3e) SHA1(365808fabc815338594aa3f7adb6ab0ec86616ef) )
	ROM_LOAD16_BYTE( "ph2750_10.b8", 0x000001, 0x010000, CRC(a28e6c88) SHA1(e57e2e05a999cf900e0cf33b5ffb966789149e29) )
ROM_END




#define J5POPTH_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "poptreashuntsound.bin", 0x000000, 0x080000, CRC(c7c3c012) SHA1(b6d4bab77ccc4499906db655326be10d346f8e6f) ) \


ROM_START( j5popth )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6512.bin", 0x000000, 0x010000, CRC(06b8eb30) SHA1(110b303521061f771775dfde30b2ae781d804ef6) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6513.bin", 0x000001, 0x010000, CRC(69498fc1) SHA1(802e05963d2911927416aa59b33d1c92bdd4a805) )
	J5POPTH_SOUND
ROM_END

ROM_START( j5poptha )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6514.bin", 0x000000, 0x010000, CRC(f128c962) SHA1(be4f0ed54e4d22f7607997d3fccdc7a82881f4a4) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6513.bin", 0x000001, 0x010000, CRC(69498fc1) SHA1(802e05963d2911927416aa59b33d1c92bdd4a805) )
	J5POPTH_SOUND
ROM_END

ROM_START( j5popthb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6516.bin", 0x000000, 0x010000, CRC(32e9a9d5) SHA1(1d3f8e9e47a2cce02cc4c3d501168b25bd7c926a) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "6513.bin", 0x000001, 0x010000, CRC(69498fc1) SHA1(802e05963d2911927416aa59b33d1c92bdd4a805) )
	J5POPTH_SOUND
ROM_END





ROM_START( j5popprz )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6245.bin", 0x000000, 0x010000, CRC(9b84c464) SHA1(ce50010893db0318112d685f2e15c3dcdef2ac2a) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6246.bin", 0x000001, 0x010000, CRC(058a0c5c) SHA1(64d65a61e8d18ec9ccff4075980326040c8aa93f) )
ROM_END

ROM_START( j5popprza )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6247.bin", 0x000000, 0x010000, CRC(6c14e636) SHA1(29b40ad11cd701da2fc5dfb6a53e3154205afc35) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6246.bin", 0x000001, 0x010000, CRC(058a0c5c) SHA1(64d65a61e8d18ec9ccff4075980326040c8aa93f) )
ROM_END



ROM_START( j5reelgh )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "r1.bin", 0x000000, 0x008000, CRC(7a251cc8) SHA1(6185ec722406996c61beeb34afe76374d464c97e) )
	ROM_LOAD16_BYTE( "r2.bin", 0x000001, 0x008000, CRC(f45704ad) SHA1(808660554d0ce1b3e9508d0a697c8fd487734395) )
	ROM_LOAD16_BYTE( "r3.bin", 0x010000, 0x008000, CRC(1d1af956) SHA1(bcecdefd662e404d6ac080b808c2766793da0748) )
	ROM_LOAD16_BYTE( "r4.bin", 0x010001, 0x008000, CRC(43441e9b) SHA1(45badcdb344a4d9df647ee99fadcd7c674e19295) )
ROM_END



ROM_START( j5roul )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "roulet0p", 0x000000, 0x010000, CRC(70863011) SHA1(70b9e971cb5d24032e3ccd2c151c8466ded655c0) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "rouletp2", 0x000001, 0x010000, CRC(25142663) SHA1(a52cbf426ca0b6ee4462b3149d68a249ce3400ba) )
ROM_END



ROM_START( j5roulcl )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6618.bin", 0x000000, 0x010000, CRC(89d0db3a) SHA1(906aa9a5ccebe236c31470c8fd93ff5f4100c929) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6619.bin", 0x000001, 0x010000, CRC(cae616f6) SHA1(1b71823a601aa5b826b0f19d11f6a5d5ce86a549) )
ROM_END

ROM_START( j5roulcla )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6620.bin", 0x000000, 0x010000, CRC(7e40f968) SHA1(89a5d9dbaa855533810957cecc242db277ee6746) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6619.bin", 0x000001, 0x010000, CRC(cae616f6) SHA1(1b71823a601aa5b826b0f19d11f6a5d5ce86a549) )
ROM_END

ROM_START( j5roulclb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6707.bin", 0x000000, 0x010000, CRC(655ff016) SHA1(335f3b0d4a4ffba55450bbf6360f8bb5dff69691) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6708.bin", 0x000001, 0x010000, CRC(ca031aa6) SHA1(f7b6b7b04d04669f82afb13cb80c078f87ef1bbb) )
ROM_END

ROM_START( j5roulclc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6709.bin", 0x000000, 0x010000, CRC(92cfd244) SHA1(6dc55008293b7e26e56e38dfedb456251fd24e2f) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6708.bin", 0x000001, 0x010000, CRC(ca031aa6) SHA1(f7b6b7b04d04669f82afb13cb80c078f87ef1bbb) )
ROM_END


#define J5SLVREE_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "gstr_032003.bin", 0x000000, 0x080000, CRC(352e28cd) SHA1(c98307f5eaf511c9d281151d1c07ffd83f24244c) ) \

ROM_START( j5slvree )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "gstr_032001.bin", 0x000000, 0x010000, CRC(e84d6437) SHA1(565b625ddb0693cd59ca1b1e07cd25ff1cb5c8f6) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "gstr_032002.bin", 0x000001, 0x010000, CRC(e7db2018) SHA1(163fd4e58da0a224e7a93619621efa4390960bc2) )
	J5SLVREE_SOUND
ROM_END

ROM_START( j5slvreea )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "silver_133001.b8", 0x000000, 0x010000, CRC(dc1c26d2) SHA1(e6443f53fc3e075b2fa13ab159b55a05c7e52740) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "silver_133002.b8", 0x000001, 0x010000, CRC(e7db2018) SHA1(163fd4e58da0a224e7a93619621efa4390960bc2) )
	J5SLVREE_SOUND
ROM_END


#define J5SLVSTR_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "sssound.bin", 0x000000, 0x080000, CRC(d4d57f9f) SHA1(2ec38b62928d8c208880015b3a5e348e9b1c2079) ) \

ROM_START( j5slvstr )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "ss30cz_02_1.b8", 0x000000, 0x010000, CRC(ea4efe3e) SHA1(3e41d5f614b386ae9d216d83d0fad080d475948d) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "ss30cz_02_2.b8", 0x000001, 0x010000, CRC(eca4b82b) SHA1(68595738290d037b820d52ccd87ef5b7cfa8de8b) )
	J5SLVSTR_SOUND
ROM_END

ROM_START( j5slvstra )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "ss75cz_02_1.b8", 0x000000, 0x010000, CRC(de1fbcdb) SHA1(7e8197a9cb0d381513af84d9a55c27d9d26918b6) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "ss30cz_02_2.b8", 0x000001, 0x010000, CRC(eca4b82b) SHA1(68595738290d037b820d52ccd87ef5b7cfa8de8b) ) // (also named ss75cz_02_2.b8)
	J5SLVSTR_SOUND
ROM_END

ROM_START( j5slvstrb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "ss75cz3_1.b8", 0x000000, 0x010000, CRC(e95dbdbc) SHA1(cf0537bb332cd9ab4ce35f7c88e9d6d6e2743fa5) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "ss30cz3_2.b8", 0x000000, 0x010000, CRC(f27ee22d) SHA1(6d26f43ef9de86b92645e8b5c1b54e6572ea330b) )
	J5SLVSTR_SOUND
ROM_END



ROM_START( j5street )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "1.1.bin", 0x000000, 0x008000, CRC(175a551e) SHA1(5ede4a59756b4e9477a711c49097e236951a5d47) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "1.2.bin", 0x000001, 0x008000, CRC(e940b0cc) SHA1(f737ed8db6d6df7a594151796a7eba27a9eb83e6) )
	ROM_LOAD16_BYTE( "1.3.bin", 0x010000, 0x008000, CRC(4bcced10) SHA1(6fa5946f213333226f517ef86854a7bc1bfd09b0) )
	ROM_LOAD16_BYTE( "1.4.bin", 0x010001, 0x008000, CRC(8cc9ab31) SHA1(a7393201843c9b7bb1a08bded9da4ea74600fc49) )
ROM_END



ROM_START( j5sup4 ) // doesn't use byte 0x81 stuff
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "super4.71",  0x000000, 0x008000, CRC(88e50a99) SHA1(7b25a280c36732b02ee60b9348cf8bf39c19adc9) )
	ROM_LOAD16_BYTE( "super4.7a2", 0x000001, 0x008000, CRC(757127ab) SHA1(7efc56dbc147814dedcb4e246d4147cbc0d8b44b) )
ROM_END


ROM_START( j5supbar )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "super_bars_20p_4_1.bin", 0x000000, 0x008000, CRC(9833523c) SHA1(3fcb5e803cd03dfd4990c5ff94aec24ac74b2813) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "supbar42.bin", 0x000001, 0x008000, CRC(0282cfde) SHA1(34a2665bdcc744f04e8b122ff4e67534ddae7c53) )
	ROM_LOAD16_BYTE( "supbar43.bin", 0x010000, 0x008000, CRC(871679fb) SHA1(dbfb95f4730e960c12578e501f15918502820153) )
	ROM_LOAD16_BYTE( "supbar44.bin", 0x010001, 0x008000, CRC(5df4f731) SHA1(953b513e08c9e979a473463373a8bbf2aef0a521) )
ROM_END

ROM_START( j5supbara )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "supbar41.bin", 0x000000, 0x008000, CRC(574ab6b8) SHA1(dea3b19adc1c8e84d62517a636d75bead36ea07a) ) // 0x81 = FB
	ROM_LOAD16_BYTE( "supbar42.bin", 0x000001, 0x008000, CRC(0282cfde) SHA1(34a2665bdcc744f04e8b122ff4e67534ddae7c53) )
	ROM_LOAD16_BYTE( "supbar43.bin", 0x010000, 0x008000, CRC(871679fb) SHA1(dbfb95f4730e960c12578e501f15918502820153) )
	ROM_LOAD16_BYTE( "supbar44.bin", 0x010001, 0x008000, CRC(5df4f731) SHA1(953b513e08c9e979a473463373a8bbf2aef0a521) )
ROM_END



ROM_START( j5suphi )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "super_hi_lo_10p_p1.bin", 0x000000, 0x008000, CRC(f6d8f14b) SHA1(42a47937f34b2278ed3f867f6be47dff8dc1413d) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "super_hi_lo_10p_p2.bin", 0x000001, 0x008000, CRC(59504077) SHA1(1b9c23f745c8b8311d33bd0b4354889d36a89fa8) )
	ROM_LOAD16_BYTE( "super_hi_lo_10p_p3.bin", 0x010000, 0x008000, CRC(c445a984) SHA1(193023c274b5794e18b46e60bba7611355598b13) )
	ROM_LOAD16_BYTE( "super_hi_lo_10p_p4.bin", 0x010001, 0x008000, CRC(132ef2d4) SHA1(ca427b8210d0b329f8256e069167d536f7782738) )
ROM_END


#define J5POPEYE_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "popsnd.bin", 0x00000, 0x80000, CRC(67378dbc) SHA1(83f87e35bb2c73a788c0ed778b33f3710eb95406) ) \

ROM_START( j5popeye ) // also found in the set marked 'Super Popeye'
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7531.bin", 0x000000, 0x10000, CRC(a8d5394c) SHA1(5be0cd8bc4cdb230a839f83e1297bc57dde20d94) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "7532.bin", 0x000001, 0x10000, CRC(5537afc2) SHA1(3e90fef908b80939c781a85a2ac9783de62d4122) )
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyea )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7533.bin", 0x000000, 0x010000, CRC(5f451b1e) SHA1(779c542fe745b1aa46e300ef327de2c08e3cb847) ) // 0x81 = FE (alt 7531.bin)
	ROM_LOAD16_BYTE( "7532.bin", 0x000001, 0x010000, CRC(5537afc2) SHA1(3e90fef908b80939c781a85a2ac9783de62d4122) )
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyeb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "popeye_8_quid_p1.bin", 0x000000, 0x010000, CRC(75e4be54) SHA1(83482a26baa7b7c7c64076f46b7656da1fb60aad) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "popeye_8_quid_p2.bin", 0x000001, 0x010000, CRC(e0b5aeba) SHA1(b99ef045f0d13ae5f885a44129fe3c7bd1ac951d) )
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyec )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "popi8ac",              0x000000, 0x010000, CRC(41b5fcb1) SHA1(73222359f3b98ca8ebcf734b95d55d6a8b0856a5) ) // 0x81 = FD (alt popeye_8_quid_p1.bin)
	ROM_LOAD16_BYTE( "popeye_8_quid_p2.bin", 0x000001, 0x010000, CRC(e0b5aeba) SHA1(b99ef045f0d13ae5f885a44129fe3c7bd1ac951d) )
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyed )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "popeye_6_a.bin", 0x000000, 0x010000, CRC(b312fa04) SHA1(e1ad46e668b71b652aa162db0f7cb832874d925c) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "popeye_6_b.bin", 0x000001, 0x010000, CRC(54a3f1d0) SHA1(63858bb56bb9127936bb2dbf889f7d8996532d9c) ) // BF<->FF unexpected 1 byte change vs popeye 6 3-2.bin @ EE28 (is one bad?)
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyee )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6287.bin",         0x000000, 0x010000, CRC(8743b8e1) SHA1(964e5d535b588ac2c24d374226a8335775c803f5) ) // 0x81 = FD (popeye_6_a.bin)
	ROM_LOAD16_BYTE( "popeye 6 3-2.bin", 0x000001, 0x010000, CRC(2858ea40) SHA1(36f33eba7cab61ea218baa17bc6540271da1632e) ) // BF<->FF unexpected 1 byte change vs popeye_6_b.bin @ EE28 (is one bad?)
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyef )	// this is very similar to the popeye(sys5)-p1.bin / popeye(sys5)-p2.bin combo, but with some extra (unused?) code?
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "popeye(sys5)-p1.bin", 0x000000, 0x010000, CRC(9396cf7b) SHA1(d4309869edd811e6cc108f90566a9313ef101636) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "popeye(sys5)-p2.bin", 0x000001, 0x010000, CRC(d9dc2cb6) SHA1(d2bf7a924a08c41a2cfc5caa32a4df0773f3a64a) )
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyeg ) // set marked 'Super Popeye'
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "s popeye 4a1.bin", 0x000000, 0x010000, CRC(e1a87bdc) SHA1(62a8c7480125b6ffa749e01b9ec822b6a6b53c0e) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "s popeye 4a2.bin", 0x000001, 0x010000, CRC(972d4ca2) SHA1(07a5a471e7db9ebcae3d72391ec870b83931b6cb) )
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyeh ) // set marked 'Super Popeye'
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "super_popeye_4_80_p1.bin", 0x000000, 0x010000, CRC(c494993b) SHA1(759e36e136a3dd16e8095c4ca3c781afbddce8db) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "super_popeye_4_80_p2.bin", 0x000001, 0x010000, CRC(b5abc01c) SHA1(2e16e932e1c3fce2bbf873aa35c0efc542492067) )
	J5POPEYE_SOUND
ROM_END

ROM_START( j5popeyei ) // set marked 'Super Popeye'
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "supo-5_1.bin", 0x000000, 0x010000, CRC(3c2da995) SHA1(3e850d1e3db98c89863a328c637f0648a24bf92f) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "supo-5_2.bin", 0x000001, 0x010000, CRC(0e9e136f) SHA1(70c2f984db79030758545b42da378719ec48ed68) )
	J5POPEYE_SOUND
ROM_END

ROM_START( j5swop )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "swop_a_fruit_test_1_1.bin", 0x000000, 0x008000, CRC(f488d5d1) SHA1(220853ae63d9a822d30836b5aacd7d7db805cb16) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "swop_a_fruit_test_1_2.bin", 0x000001, 0x008000, CRC(36b7b460) SHA1(fc1fa688c58f490d399d9bca7108f811c20c726f) )
	ROM_LOAD16_BYTE( "swop_a_fruit_test_1_3.bin", 0x010000, 0x008000, CRC(f8321c28) SHA1(91efabaeadb8667cc4f1644c1f714f87b20a83ba) )
	ROM_LOAD16_BYTE( "swop_a_fruit_test_1_4.bin", 0x010001, 0x008000, CRC(b96c90ec) SHA1(ce03a93b5372e20ee038dae3c23845b56ed9e37c) )
ROM_END


ROM_START( j5term )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "terminator_3_1.bin", 0x000000, 0x008000, CRC(fabf49d6) SHA1(189af4cc5272a7a7d2666b119b1c48b1f83c9873) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "terminator_3_2.bin", 0x000001, 0x008000, CRC(59bf400e) SHA1(3e487c67aa5bc9a52029a79606680af9602010dd) )
	ROM_LOAD16_BYTE( "terminator_3_3.bin", 0x010000, 0x008000, CRC(1a2fc873) SHA1(9f70af0929e059341b338165698ca6e75aba51a7) )
	ROM_LOAD16_BYTE( "terminator_3_4.bin", 0x010001, 0x008000, CRC(9f6e25ff) SHA1(c1dd2d7ad644a285cfbc6e30f9ccb25f4dbc2f08) )
ROM_END


ROM_START( j5topshp )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "tots_3_1.bin", 0x000000, 0x008000, CRC(882f1249) SHA1(4bf16267ba9c6229607323fa72e3755f3ab9618a) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "tots_3_2.bin", 0x000001, 0x008000, CRC(19022be3) SHA1(0e92a7aa75850a35028333e307a628931eea4a09) )
	ROM_LOAD16_BYTE( "tots_3_3.bin", 0x010000, 0x008000, CRC(26088a7d) SHA1(85e51de92ca60baca554c9ecd847d73734b5cac1) )
	ROM_LOAD16_BYTE( "tots_3_4.bin", 0x010001, 0x008000, CRC(30f05078) SHA1(5f1da4c88e6bdeafea96d95eccfc7708ec5fd9ad) )
ROM_END


ROM_START( j5trail )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6435.bin", 0x000000, 0x008000, CRC(e6331620) SHA1(27941aa4df54eacd258800c1198aeed63efe007d) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "6436.bin", 0x000001, 0x008000, CRC(df8898e8) SHA1(07261a2c40b48ff62201b44ddf19f338220f3c21) )
	ROM_LOAD16_BYTE( "6437.bin", 0x010000, 0x008000, CRC(77bd6f6c) SHA1(739f655676b00c5ef0dc55bb20f8dd736f33b37d) )
	ROM_LOAD16_BYTE( "6438.bin", 0x010001, 0x008000, CRC(87868413) SHA1(ad74733b915dbd5d8ebb6e8fe90d05501b993d50) )
ROM_END


ROM_START( j5traila )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6439.bin", 0x000000, 0x008000, CRC(d5ed6f01) SHA1(3afebe75a7cf3480140bc39a789e80d7d8c9c11d) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "6436.bin", 0x000001, 0x008000, CRC(df8898e8) SHA1(07261a2c40b48ff62201b44ddf19f338220f3c21) )
	ROM_LOAD16_BYTE( "6437.bin", 0x010000, 0x008000, CRC(77bd6f6c) SHA1(739f655676b00c5ef0dc55bb20f8dd736f33b37d) )
	ROM_LOAD16_BYTE( "6438.bin", 0x010001, 0x008000, CRC(87868413) SHA1(ad74733b915dbd5d8ebb6e8fe90d05501b993d50) )
ROM_END

ROM_START( j5trailb ) // if this is really a trailblazer set it's missing all the other roms, the only 68k rom present doesn't fit with the others
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "trailblazer_1_1.bin", 0x000000, 0x008000, CRC(df527554) SHA1(71f0d0e0bf9326cd51bf8d067dda99f0020622da) ) //  // 0x81 = FF (mismatched 68k rom)
	ROM_LOAD16_BYTE( "trailblazer_1_2.bin", 0x000001, 0x008000, NO_DUMP )
	ROM_LOAD16_BYTE( "trailblazer_1_3.bin", 0x010000, 0x008000, NO_DUMP )
	ROM_LOAD16_BYTE( "trailblazer_1_4.bin", 0x010001, 0x008000, NO_DUMP )
ROM_END



ROM_START( j5td ) // doesn't use byte 0x81 stuff (byte 0x40 of tupd1 instead?)
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "tudp1.bin", 0x000000, 0x008000, CRC(48fbf7cd) SHA1(a5b2b4eae1d24f9f8683a783795e3a26f5a32386) )
	ROM_LOAD16_BYTE( "tudp2.bin", 0x000001, 0x008000, CRC(f41d583a) SHA1(d60f70648a98a0066bcdf115cbd8d45230fd3af9) )
ROM_END


#define J5UJ_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "unjasnd.bin", 0x000000, 0x040000, CRC(e538212a) SHA1(873a7e32041cb831784634306b13934d7aa697dc) )

ROM_START( j5uj )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "union_jackpot_4g1.bin", 0x000000, 0x010000, CRC(e2fa3cb7) SHA1(05be9c397c7645838c0df4f0cf69a9f38a5cbcdb) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "union_jackpot_4g2.bin", 0x000001, 0x010000, CRC(0e87e5fe) SHA1(20f8d04a2fc34e67a7cc1f82fb531286caf7d42c) )
	J5UJ_SOUND
ROM_END

ROM_START( j5uja )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "union_jackpot_1.bin", 0x000000, 0x010000, CRC(f813f15a) SHA1(9a40073a20f9824a2767e67666c76d527e91b9c1) ) // 0x81 = BF
	ROM_LOAD16_BYTE( "union_jackpot_2.bin", 0x000001, 0x010000, CRC(e0daeaa9) SHA1(5212bb9ac3485f7029bbed781f51e0388792c3f1) )
	J5UJ_SOUND
ROM_END

ROM_START( j5ujb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "unja3.1.bin", 0x000000, 0x010000, CRC(a58ba24f) SHA1(a6b59d9cdf43f6341b47fa972176641756c2f6be) ) // 0x81 = BF
	ROM_LOAD16_BYTE( "unja3.2.bin", 0x000001, 0x010000, CRC(d124daba) SHA1(c9aa5a9fb6b751d281eb4b5c05ad9c0f4b726a68) )
	J5UJ_SOUND
ROM_END




ROM_START( j5wsc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "wall_street_5_1.bin", 0x000000, 0x008000, CRC(31533aba) SHA1(3067bfc80430f3f3f7b29300cafec1691726a310) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "wall_street_5_2.bin", 0x000001, 0x008000, CRC(d28fbdd3) SHA1(037e4013bd0f1250c097751f8023e1c89ea49dba) )
	ROM_LOAD16_BYTE( "wall_street_5_3.bin", 0x010000, 0x008000, CRC(a56580da) SHA1(3bc4389bd12cae68757fefb520ea730cd507c2c0) )
	ROM_LOAD16_BYTE( "wall_street_5_4.bin", 0x010001, 0x008000, CRC(34fd8e5f) SHA1(bc61fd07355b087fc354ab71cdfd4a8539b43403) )
ROM_END

ROM_START( j5wsca )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "wstrtp1", 0x000000, 0x008000, CRC(491af07c) SHA1(b0f7e75ef68ec17b29ab9e4b9816a29046a8c372) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "wstrtp2", 0x000001, 0x008000, CRC(102d7817) SHA1(0ff093f63f80fd7e26f44e9079bb9a424e364190) )
	ROM_LOAD16_BYTE( "wstrtp3", 0x010000, 0x008000, CRC(7963c0ad) SHA1(e80fb3f5c082ed8e130cd329657fa6ac0019b1ce) )
	ROM_LOAD16_BYTE( "wstrtp4", 0x010001, 0x008000, CRC(5a6a81be) SHA1(83cd7471d47541f07691918a208a65353d13298c) )
ROM_END



#define J5HAGAR_SOUND \
	ROM_REGION( 0x80000, "upd7759", 0 ) \
	ROM_LOAD( "6186.bin", 0x0000, 0x080000, CRC(3bdb52c8) SHA1(0b83890609fad4f2641844d9bd5504996ad2cc10) ) \

ROM_START( j5hagar )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "hagar_6_a.bin", 0x000000, 0x010000, CRC(f461d173) SHA1(1b991cef0e1480cf1ee390f9d1da521660263501) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "hagar_6_b.bin", 0x000001, 0x010000, CRC(bcb26e64) SHA1(17a4e53caa16263444cfe87b35c675da90736292) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5hagara )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6367.bin",      0x000000, 0x010000, CRC(03f1f321) SHA1(02ed908115814d5b50cc6d3edfb396d8c2af25a3) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "hagar_6_b.bin", 0x000001, 0x010000, CRC(bcb26e64) SHA1(17a4e53caa16263444cfe87b35c675da90736292) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5hagarb )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "6369.bin",      0x000000, 0x010000, CRC(c0309396) SHA1(e4ac2c60953546c0b6e5ef150fb08d245cb6516f) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "hagar_6_b.bin", 0x000001, 0x010000, CRC(bcb26e64) SHA1(17a4e53caa16263444cfe87b35c675da90736292) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5hagarc )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "hagar_20p_6_quid_cash_p1.bin", 0x000000, 0x010000, CRC(c8fc9296) SHA1(c973ab18173ec0c33959b803347193e08ab53565) ) // 0x81 = BF
	ROM_LOAD16_BYTE( "hagar_6_b.bin",                0x000001, 0x010000, CRC(bcb26e64) SHA1(17a4e53caa16263444cfe87b35c675da90736292) )

	J5HAGAR_SOUND
ROM_END


ROM_START( j5hagard )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7234.bin", 0x000000, 0x010000, CRC(4cc9edf4) SHA1(4febb4aef68c4c2f171b3410ca4eb253466e1a78) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "7235.bin", 0x000001, 0x010000, CRC(df5513c8) SHA1(e052b08a0690dcb12f8a18eca7a9ccd2b8528d70) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5hagare )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7236.bin", 0x000000, 0x010000, CRC(bb59cfa6) SHA1(1489c092e5b624fa8dc1737e13e88a9f11dd167c) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "7235.bin", 0x000001, 0x010000, CRC(df5513c8) SHA1(e052b08a0690dcb12f8a18eca7a9ccd2b8528d70) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5hagarf )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7237.bin", 0x000000, 0x010000, CRC(7054ae11) SHA1(9a020ef4a5656bfd1962e67c726cb26d2b8c691b) ) // 0x81 = BF
	ROM_LOAD16_BYTE( "7235.bin", 0x000001, 0x010000, CRC(df5513c8) SHA1(e052b08a0690dcb12f8a18eca7a9ccd2b8528d70) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5hagarg )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7397.bin", 0x000000, 0x010000, CRC(7898af11) SHA1(203abc70aa2a580fda4986ff6d344c5323df75b1) ) // 0x81 = FD
	ROM_LOAD16_BYTE( "7235.bin", 0x000001, 0x010000, CRC(df5513c8) SHA1(e052b08a0690dcb12f8a18eca7a9ccd2b8528d70) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5hagarh )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7702.bin", 0x000000, 0x010000, CRC(cc38904e) SHA1(e9125170e064d7a957fa282943f540c928896a8f) ) // 0x81 = FF
	ROM_LOAD16_BYTE( "7703.bin", 0x000001, 0x010000, CRC(8f987d72) SHA1(68cdf7beb1fa7d5a1a60311762a346f2ef513c85) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5hagari )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "7704.bin", 0x000000, 0x010000, CRC(3ba8b21c) SHA1(9de8b49662735c9f2392aa3983e3fe741c24666a) ) // 0x81 = FE
	ROM_LOAD16_BYTE( "7703.bin", 0x000001, 0x010000, CRC(8f987d72) SHA1(68cdf7beb1fa7d5a1a60311762a346f2ef513c85) )

	J5HAGAR_SOUND
ROM_END


ROM_START( j5hagarj ) // 9 bytes total difference from the 7702/7703 pairing, bad? hacked?
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "hagar_1.bin", 0x000000, 0x010000, CRC(f80ab58e) SHA1(f22b11d3d9c5f9bd908df900ba1a2b04bde93fe9) )
	ROM_LOAD16_BYTE( "hagar_2.bin", 0x000001, 0x010000, CRC(fe9cf2af) SHA1(adf4abda87d2b6c2e0c9c2d481d69d01cceae509) )

	J5HAGAR_SOUND
ROM_END

ROM_START( j5tstal )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "5834.bin", 0x000000, 0x008000, CRC(57c71966) SHA1(5cc84943aaad9c38dcdd9ae0535cecfd24ad5215) )
	ROM_LOAD16_BYTE( "5835.bin", 0x000001, 0x008000, CRC(4c8c07c9) SHA1(7a4bbee98551e4ecd6045239a2828d26d9768e19) )
ROM_END


ROM_START( j5tst1 )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "4065.bin", 0x000000, 0x008000, CRC(a2adb912) SHA1(9d2846b54ed2fdfca25a615ce7b96201fa36cb5a) )
	ROM_LOAD16_BYTE( "4066.bin", 0x000001, 0x008000, CRC(c8f2808c) SHA1(9922fff235f972e2e3e58305abf76e41a41275f2) )
	ROM_LOAD16_BYTE( "4067.bin", 0x010000, 0x008000, CRC(e19dc859) SHA1(62d75e48be960e0e4006378cb9db6fc305c7eae9) )
	ROM_LOAD16_BYTE( "4068.bin", 0x010001, 0x008000, CRC(60a3af9b) SHA1(02fc1db698833726e3de26671ef5bdc74aa1f749) )
ROM_END

ROM_START( j5tst2 )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "4519.bin", 0x000000, 0x008000, CRC(c3ecc4f2) SHA1(013368b0196c8aa4b10a4a365601be409dc81e0c) )
	ROM_LOAD16_BYTE( "4520.bin", 0x000001, 0x008000, CRC(8de74e00) SHA1(0a410eea23429876bfdba36c7a511d17b8b469e0) )
ROM_END


ROM_START( j5movie )
	ROM_REGION( 0x300000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "mm1.bin", 0x0000, 0x010000, CRC(3f2e67e0) SHA1(153ceee8054077d6983e7476e295a9e148fb63b0) )
	ROM_LOAD16_BYTE( "mm2.bin", 0x0000, 0x010000, CRC(051a157f) SHA1(c07bc77a5ee9b41f6e4976af8bab147fe6c16920) )

	ROM_REGION( 0x80000, "upd7759", 0 )
	ROM_LOAD( "mmsnd.bin", 0x0000, 0x040000, CRC(87ca1d90) SHA1(e94826ec6a3f8aa5955965af56f50ddfa087dc72) )
ROM_END

ROM_START( j5nudfic )
	ROM_REGION( 0x1000000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "nudge_fiction_alt_p1.bin", 0x0000, 0x010000, CRC(4ffc3d35) SHA1(50cce1719f1354798feb4a4fad184234d29a5f20) )
	ROM_LOAD16_BYTE( "nudge_fiction_alt_p2.bin", 0x0001, 0x010000, CRC(c9854c0b) SHA1(7aee21ee2de5ddd74a99fc6dc176912eac6a8dd6) )
ROM_END

ROM_START( j5revo )
	ROM_REGION( 0x1000000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "revolver_5p_p1.bin", 0x00000, 0x010000, CRC(306274fd) SHA1(a43fc1d4d454e75d11bc1e1823cebc013523124c) )
	ROM_LOAD16_BYTE( "revolver_5p_p2.bin", 0x00001, 0x010000, CRC(cb6feb77) SHA1(2e4be15366d94d01585232ce60e89e46fbad1bf0) )
ROM_END

ROM_START( j5revoa )
	ROM_REGION( 0x1000000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "revolver_v1_p1.bin", 0x00000, 0x010000, CRC(f7b819cf) SHA1(dd08972dd6c51505adec44f1bc56195315594900) )
	ROM_LOAD16_BYTE( "revolver_v1_p2.bin", 0x00001, 0x010000, CRC(dabb1dfc) SHA1(94153fabd5fd2c2546e8b54bcc43131ebd886ed4) )
ROM_END



ROM_START( j5sizl )
	ROM_REGION( 0x1000000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "sizzling_s_1a1.bin", 0x00000, 0x008000, CRC(f28a0167) SHA1(ef09b24e65583e1cd6502afd244958bf567e6e4c) )
	ROM_LOAD16_BYTE( "sizzling_s_1a2.bin", 0x00001, 0x008000, CRC(336f380f) SHA1(f7c8aa1afaeb4dc49f02bcff21667f9065955dcd) )
	ROM_LOAD16_BYTE( "sizzling_s_1a3.bin", 0x10000, 0x008000, CRC(efbb7a18) SHA1(dcaf966b9383542addafe4d6c86bd9e57b48f623) )
	ROM_LOAD16_BYTE( "sizzling_s_1a4.bin", 0x10001, 0x008000, CRC(acb7c535) SHA1(78a1fbc05f26be1cb85ed4a75d0fe91f86776557) )
ROM_END

ROM_START( j5hilos )
	ROM_REGION( 0x1000000, "maincpu", 0 )
	ROM_LOAD16_BYTE( "hihosilver1.1.bin", 0x00000, 0x008000, CRC(e8b5532d) SHA1(45a62028649cde884f313cfabadee994e13f1ea3) )
	ROM_LOAD16_BYTE( "hihosilver1.2.bin", 0x00001, 0x008000, CRC(8cffe4ac) SHA1(c81ea16fca08f6f2daa8ff629a0f53e7a641c792) )
	ROM_LOAD16_BYTE( "hihosilver1.3.bin", 0x10000, 0x008000, CRC(ea18c31f) SHA1(a6ac9e2e70cb156e453821723f3eed23b4ade3c4) )
	ROM_LOAD16_BYTE( "hihosilver1.4.bin", 0x10001, 0x008000, NO_DUMP ) // seems to be missing
ROM_END





/* Video based titles */
GAME( 1994, monopoly	, 0			, jpmsys5v, monopoly, 0, ROT0, "JPM", "Monopoly (Jpm) (SYSTEM5 VIDEO, set 1)",         0 )
GAME( 1994, monopolya	, monopoly	, jpmsys5v, monopoly, 0, ROT0, "JPM", "Monopoly (Jpm) (SYSTEM5 VIDEO, set 2)",         0 )
GAME( 1995, monoplcl	, monopoly	, jpmsys5v, monopoly, 0, ROT0, "JPM", "Monopoly Classic (Jpm) (SYSTEM5 VIDEO)", 0 )
GAME( 1995, monopldx	, 0			, jpmsys5v, monopoly, 0, ROT0, "JPM", "Monopoly Deluxe (Jpm) (SYSTEM5 VIDEO)",  0 )
GAME( 199?, cashcade	, 0			, jpmsys5v, monopoly, 0, ROT0, "JPM", "Cashcade (Jpm) (SYSTEM5 VIDEO)", GAME_NOT_WORKING|GAME_NO_SOUND ) // shows a loading error.. is the set incomplete?


#define GAME_FLAGS GAME_NOT_WORKING|GAME_REQUIRES_ARTWORK|GAME_MECHANICAL|GAME_NO_SOUND

/* Non-Video */
GAME( 199?, j5tstal		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "JPM System 5 Alpha Display Test Utility (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5tst1		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "JPM System 5 Test Set (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5tst2		, j5tst1	, jpmsys5, popeye, 0, ROT0, "JPM", "JPM System 5 Test Set (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )

GAME( 199?, j5fifth		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "5th Avenue (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5ar80		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World In Eighty Days (Jpm) (SYSTEM5, set 1)", GAME_FLAGS ) // This was also listed as by 'Crystal'.  There was Crystal ROM in the set, but it wasn't an JPM SYS5 rom...
GAME( 199?, j5ar80a		, j5ar80	, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World In Eighty Days (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5ar80b		, j5ar80	, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World In Eighty Days (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5ar80c		, j5ar80	, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World In Eighty Days (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5ar80d		, j5ar80	, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World In Eighty Days (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5ar80cl	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World Club (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5ar80cla	, j5ar80cl	, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World Club (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5ar80clb	, j5ar80cl	, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World Club (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5ar80clc	, j5ar80cl	, jpmsys5, popeye, 0, ROT0, "JPM", "Around The World Club (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5buc		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Buccaneer (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5cir		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Circus (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5cira		, j5cir		, jpmsys5, popeye, 0, ROT0, "JPM", "Circus (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5cirb		, j5cir		, jpmsys5, popeye, 0, ROT0, "JPM", "Circus (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5circ		, j5cir		, jpmsys5, popeye, 0, ROT0, "JPM", "Circus (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5cird		, j5cir		, jpmsys5, popeye, 0, ROT0, "JPM", "Circus (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5clbnud	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Club Nudger (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5clbnuda	, j5clbnud	, jpmsys5, popeye, 0, ROT0, "JPM", "Club Nudger (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5daytn		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Daytona (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5daytna	, j5daytn	, jpmsys5, popeye, 0, ROT0, "JPM", "Daytona (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5daycls	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Daytona Classic (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5dayclsa	, j5daycls	, jpmsys5, popeye, 0, ROT0, "JPM", "Daytona Classic (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5dirty		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Dirty Dozen (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5dirtya	, j5dirty	, jpmsys5, popeye, 0, ROT0, "JPM", "Dirty Dozen (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5dirtyb	, j5dirty	, jpmsys5, popeye, 0, ROT0, "JPM", "Dirty Dozen (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5dirtyc	, j5dirty	, jpmsys5, popeye, 0, ROT0, "JPM", "Dirty Dozen (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5fairgd	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground Attraction Club (Jpm) (SYSTEM5, set 1)", GAME_FLAGS ) // or just 'Fairground' ?
GAME( 199?, j5fairgda	, j5fairgd	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground Attraction Club (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5fairgdb	, j5fairgd	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground Attraction Club (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5fairgdc	, j5fairgd	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground Attraction Club (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5fairgdd	, j5fairgd	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground Attraction Club (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5fairgde	, j5fairgd	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground Attraction Club (Jpm) (SYSTEM5, set 6)", GAME_FLAGS )
GAME( 199?, j5fair		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5faira		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5fairb		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5fairc		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5faird		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5faire		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 6)", GAME_FLAGS )
GAME( 199?, j5fairf		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 7)", GAME_FLAGS )
GAME( 199?, j5fairg		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 8)", GAME_FLAGS )
GAME( 199?, j5fairh		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 9)", GAME_FLAGS )
GAME( 199?, j5fairi		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 10)", GAME_FLAGS )
GAME( 199?, j5fairj		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 11)", GAME_FLAGS )
GAME( 199?, j5fairk		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 12)", GAME_FLAGS )
GAME( 199?, j5fairl		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 13)", GAME_FLAGS )
GAME( 199?, j5fairm		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 14)", GAME_FLAGS )
GAME( 199?, j5fairn		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 15)", GAME_FLAGS )
GAME( 199?, j5fairo		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 16)", GAME_FLAGS )
GAME( 199?, j5fairp		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 17)", GAME_FLAGS )
GAME( 199?, j5fairq		, j5fair	, jpmsys5, popeye, 0, ROT0, "JPM", "Fairground (Jpm) (SYSTEM5, set 18)", GAME_FLAGS )
GAME( 199?, j5filth		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5filtha	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5filthb	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5filthc	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5filthd	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5filthe	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 6)", GAME_FLAGS )
GAME( 199?, j5filthf	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 7)", GAME_FLAGS )
GAME( 199?, j5filthg	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 8)", GAME_FLAGS )
GAME( 199?, j5filthh	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 9)", GAME_FLAGS )
GAME( 199?, j5filthi	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 10)", GAME_FLAGS )
GAME( 199?, j5filthj	, j5filth	, jpmsys5, popeye, 0, ROT0, "JPM", "Filthy Rich (Jpm) (SYSTEM5, set 11)", GAME_FLAGS )
GAME( 199?, j5firebl	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Fireball (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5firebla	, j5firebl	, jpmsys5, popeye, 0, ROT0, "JPM", "Fireball (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5fireblb	, j5firebl	, jpmsys5, popeye, 0, ROT0, "JPM", "Fireball (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5frmag		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Fruit Magic (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5goldbr	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Golden Bars (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5hagar		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5hagara	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5hagarb	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5hagarc	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5hagard	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5hagare	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 6)", GAME_FLAGS )
GAME( 199?, j5hagarf	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 7)", GAME_FLAGS )
GAME( 199?, j5hagarg	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 8)", GAME_FLAGS )
GAME( 199?, j5hagarh	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 9)", GAME_FLAGS )
GAME( 199?, j5hagari	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 10)", GAME_FLAGS )
GAME( 199?, j5hagarj	, j5hagar	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar (Jpm) (SYSTEM5, set 11)", GAME_FLAGS )
GAME( 199?, j5hagsho	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar Showcase (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5hagshoa	, j5hagsho	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar Showcase (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5hagshob	, j5hagsho	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar Showcase (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5hagshoc	, j5hagsho	, jpmsys5, popeye, 0, ROT0, "JPM", "Hagar Showcase (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5holly		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Hollywood Nights (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5hollya	, j5holly	, jpmsys5, popeye, 0, ROT0, "JPM", "Hollywood Nights (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5hollyb	, j5holly	, jpmsys5, popeye, 0, ROT0, "JPM", "Hollywood Nights (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5hollyc	, j5holly	, jpmsys5, popeye, 0, ROT0, "JPM", "Hollywood Nights (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5hollyd	, j5holly	, jpmsys5, popeye, 0, ROT0, "JPM", "Hollywood Nights (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5hollye	, j5holly	, jpmsys5, popeye, 0, ROT0, "JPM", "Hollywood Nights (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5hotdog	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Hot Dogs (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5hotdoga	, j5hotdog	, jpmsys5, popeye, 0, ROT0, "JPM", "Hot Dogs (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5indsum	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Indian Summer (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5indy		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Indy 500 (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5intr		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Intrigue (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5intra		, j5intr	, jpmsys5, popeye, 0, ROT0, "JPM", "Intrigue (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5intrb		, j5intr	, jpmsys5, popeye, 0, ROT0, "JPM", "Intrigue (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5intrc		, j5intr	, jpmsys5, popeye, 0, ROT0, "JPM", "Intrigue (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5jokgld	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5jokglda	, j5jokgld	, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5jokgldb	, j5jokgld	, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5jokgldc	, j5jokgld	, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5jokgldd	, j5jokgld	, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5jokglde	, j5jokgld	, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 6)", GAME_FLAGS )
GAME( 199?, j5jokgldf	, j5jokgld	, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 7)", GAME_FLAGS )
GAME( 199?, j5jokgldg	, j5jokgld	, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 8)", GAME_FLAGS )
GAME( 199?, j5jokgldh	, j5jokgld	, jpmsys5, popeye, 0, ROT0, "JPM", "Jokers Gold (Jpm) (SYSTEM5, set 9)", GAME_FLAGS )
GAME( 199?, j5nite		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Nite Club (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5nitea		, j5nite	, jpmsys5, popeye, 0, ROT0, "JPM", "Nite Club (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5palm		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Palm Springs (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5palma		, j5palm	, jpmsys5, popeye, 0, ROT0, "JPM", "Palm Springs (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5phnx		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Phoenix (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5phnxa		, j5phnx	, jpmsys5, popeye, 0, ROT0, "JPM", "Phoenix (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5popeye	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 1)", GAME_FLAGS ) // (20p/8 GBP Token) ?
GAME( 199?, j5popeyea	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5popeyeb	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5popeyec	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5popeyed	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 5)", GAME_FLAGS )
GAME( 199?, j5popeyee	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 6)", GAME_FLAGS )
GAME( 199?, j5popeyef	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 7)", GAME_FLAGS )
GAME( 199?, j5popeyeg	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 8)", GAME_FLAGS )
GAME( 199?, j5popeyeh	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 9)", GAME_FLAGS )
GAME( 199?, j5popeyei	, j5popeye	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye (Jpm) (SYSTEM5, set 10)", GAME_FLAGS )
GAME( 199?, j5popth		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye's Treasure Hunt (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5poptha	, j5popth	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye's Treasure Hunt (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5popthb	, j5popth	, jpmsys5, popeye, 0, ROT0, "JPM", "Popeye's Treasure Hunt (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5popprz	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Prize Popeye Vending (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5popprza	, j5popprz	, jpmsys5, popeye, 0, ROT0, "JPM", "Prize Popeye Vending (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5reelgh	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Reel Ghost (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5roul		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Roulette (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5roulcl	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Roulette Club (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5roulcla	, j5roulcl	, jpmsys5, popeye, 0, ROT0, "JPM", "Roulette Club (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5roulclb	, j5roulcl	, jpmsys5, popeye, 0, ROT0, "JPM", "Roulette Club (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5roulclc	, j5roulcl	, jpmsys5, popeye, 0, ROT0, "JPM", "Roulette Club (Jpm) (SYSTEM5, set 4)", GAME_FLAGS )
GAME( 199?, j5slvree	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Silver Reels (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5slvreea	, j5slvree	, jpmsys5, popeye, 0, ROT0, "JPM", "Silver Reels (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5slvstr	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Silver Streak (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5slvstra	, j5slvstr	, jpmsys5, popeye, 0, ROT0, "JPM", "Silver Streak (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5slvstrb	, j5slvstr	, jpmsys5, popeye, 0, ROT0, "JPM", "Silver Streak (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5street	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Streetwise (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5sup4		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Super 4 (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5supbar	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Super Bars (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5supbara	, j5supbar	, jpmsys5, popeye, 0, ROT0, "JPM", "Super Bars (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5suphi		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Super Hi-Lo (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5swop		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Swop A Fruit Club (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5term		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Terminator (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5topshp	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Top Of The Shop Club (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5trail		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Trailblazer Club (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5traila	, j5trail	, jpmsys5, popeye, 0, ROT0, "JPM", "Trailblazer Club (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5trailb	, j5trail	, jpmsys5, popeye, 0, ROT0, "JPM", "Trailblazer Club (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5td		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Tumbling Dice (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5uj		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Union Jackpot (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5uja		, j5uj		, jpmsys5, popeye, 0, ROT0, "JPM", "Union Jackpot (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5ujb		, j5uj		, jpmsys5, popeye, 0, ROT0, "JPM", "Union Jackpot (Jpm) (SYSTEM5, set 3)", GAME_FLAGS )
GAME( 199?, j5wsc		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Wall Street Club (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5wsca		, j5wsc		, jpmsys5, popeye, 0, ROT0, "JPM", "Wall Street Club (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )

GAME( 199?, j5movie		, 0			, jpmsys5, popeye, 0, ROT0, "Crystal", "Movie Magic Club (Crystal) (SYSTEM5)", GAME_FLAGS ) // apparently by Crystal

GAME( 199?, j5nudfic	, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Nudge Fiction (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5revo		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Revolver (Jpm) (SYSTEM5, set 1)", GAME_FLAGS )
GAME( 199?, j5revoa		, j5revo	, jpmsys5, popeye, 0, ROT0, "JPM", "Revolver (Jpm) (SYSTEM5, set 2)", GAME_FLAGS )
GAME( 199?, j5sizl		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Sizzling (Jpm) (SYSTEM5)", GAME_FLAGS )
GAME( 199?, j5hilos		, 0			, jpmsys5, popeye, 0, ROT0, "JPM", "Hi Lo Silver (Jpm) (SYSTEM5)", GAME_FLAGS )
