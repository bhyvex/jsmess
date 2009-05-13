/***************************************************************************

	machine/apple3.c

	Apple ///


	VIA #0 (D VIA)
		CA1:	1 if a cartridge is inserted, 0 otherwise

	VIA #1 (E VIA)
		CA2:	1 if key pressed, 0 otherwise

***************************************************************************/

#include "driver.h"
#include "includes/apple3.h"
#include "includes/apple2.h"
#include "cpu/m6502/m6502.h"
#include "machine/6522via.h"
#include "machine/ay3600.h"
#include "machine/applefdc.h"
#include "devices/appldriv.h"
#include "machine/6551.h"


UINT32 apple3_flags;

static void apple3_update_drives(const device_config *device);

static UINT8 via_0_a;
static UINT8 via_0_b;
static UINT8 via_1_a;
static UINT8 via_1_b;
static int via_1_irq;
static int apple3_enable_mask;
static offs_t zpa;

#define LOG_MEMORY		1
#define LOG_INDXADDR	1



/* ----------------------------------------------------------------------- */

static int profile_lastaddr;
static UINT8 profile_gotstrobe;
static UINT8 profile_readdata;
static UINT8 profile_busycount;
static UINT8 profile_busy;
static UINT8 profile_online;
static UINT8 profile_writedata;

static void apple3_profile_init(void)
{
}



static void apple3_profile_statemachine(void)
{
}



static UINT8 apple3_profile_r(offs_t offset)
{
	UINT8 result = 0;

	offset %= 4;
	apple3_profile_statemachine();

	profile_lastaddr = offset;

	switch(offset)
	{
		case 1:
			profile_gotstrobe = 1;
			apple3_profile_statemachine();
			result = profile_readdata;
			break;

		case 2:
			if (profile_busycount > 0)
			{
				profile_busycount--;
				result = 0xFF;
			}
			else
			{
				result = profile_busy | profile_online;
			}
			break;
	}
	return result;
}



static void apple3_profile_w(offs_t offset, UINT8 data)
{
	offset %= 4;
	profile_lastaddr = -1;

	switch(offset)
	{
		case 0:
			profile_writedata = data;
			profile_gotstrobe = 1;
			break;
	}
	apple3_profile_statemachine();
}



/* ----------------------------------------------------------------------- */

static READ8_HANDLER( apple3_c0xx_r )
{
	const device_config *acia = devtag_get_device(space->machine, "acia");
	const device_config *fdc = devtag_get_device(space->machine, "fdc");
	UINT8 result = 0xFF;

	switch(offset)
	{
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x04: case 0x05: case 0x06: case 0x07:
			result = AY3600_keydata_strobe_r();
			break;

		case 0x08: case 0x09: case 0x0A: case 0x0B:
		case 0x0C: case 0x0D: case 0x0E: case 0x0F:
			/* modifier keys */
			result = 0x7d;
			break;

		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x18: case 0x19: case 0x1A: case 0x1B:
		case 0x1C: case 0x1D: case 0x1E: case 0x1F:
			AY3600_anykey_clearstrobe_r();
			break;

		case 0x50: case 0x51: case 0x52: case 0x53:
		case 0x54: case 0x55: case 0x56: case 0x57:
			/* graphics softswitches */
			if (offset & 1)
				apple3_flags |= 1 << ((offset - 0x50) / 2);
			else
				apple3_flags &= ~(1 << ((offset - 0x50) / 2));
			break;

		case 0x60: case 0x61: case 0x62: case 0x63:
		case 0x64: case 0x65: case 0x66: case 0x67:
		case 0x68: case 0x69: case 0x6A: case 0x6B:
		case 0x6C: case 0x6D: case 0x6E: case 0x6F:
			/* unsure what these are */
			result = 0x00;
			break;

		case 0xC0: case 0xC1: case 0xC2: case 0xC3:
		case 0xC4: case 0xC5: case 0xC6: case 0xC7:
		case 0xC8: case 0xC9: case 0xCA: case 0xCB:
		case 0xCC: case 0xCD: case 0xCE: case 0xCF:
			/* profile */
			result = apple3_profile_r(offset);
			break;

		case 0xD0: case 0xD1: case 0xD2: case 0xD3:
		case 0xD4: case 0xD5: case 0xD6: case 0xD7:
			/* external drive stuff */
			if (offset & 1)
				apple3_flags |= VAR_EXTA0 << ((offset - 0xD0) / 2);
			else
				apple3_flags &= ~(VAR_EXTA0 << ((offset - 0xD0) / 2));
			apple3_update_drives((device_config*)devtag_get_device(space->machine, "fdc"));
			result = 0x00;
			break;

		case 0xDB:
			apple3_write_charmem(space->machine);
			break;

		case 0xE0: case 0xE1: case 0xE2: case 0xE3:
		case 0xE4: case 0xE5: case 0xE6: case 0xE7:
		case 0xE8: case 0xE9: case 0xEA: case 0xEB:
		case 0xEC: case 0xED: case 0xEE: case 0xEF:
			result = applefdc_r(fdc, offset);
			break;

		case 0xF0:
		case 0xF1:
		case 0xF2:
		case 0xF3:
			result = acia_6551_r(acia, offset & 0x03);
			break;
	}
	return result;
}



static WRITE8_HANDLER( apple3_c0xx_w )
{
	const device_config *acia = devtag_get_device(space->machine, "acia");
	const device_config *fdc = devtag_get_device(space->machine, "fdc");
	switch(offset)
	{
		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x18: case 0x19: case 0x1A: case 0x1B:
		case 0x1C: case 0x1D: case 0x1E: case 0x1F:
			AY3600_anykey_clearstrobe_r();
			break;

		case 0x50: case 0x51: case 0x52: case 0x53:
		case 0x54: case 0x55: case 0x56: case 0x57:
			/* graphics softswitches */
			if (offset & 1)
				apple3_flags |= 1 << ((offset - 0x50) / 2);
			else
				apple3_flags &= ~(1 << ((offset - 0x50) / 2));
			break;

		case 0xC0: case 0xC1: case 0xC2: case 0xC3:
		case 0xC4: case 0xC5: case 0xC6: case 0xC7:
		case 0xC8: case 0xC9: case 0xCA: case 0xCB:
		case 0xCC: case 0xCD: case 0xCE: case 0xCF:
			/* profile */
			apple3_profile_w(offset, data);
			break;

		case 0xD0: case 0xD1: case 0xD2: case 0xD3:
		case 0xD4: case 0xD5: case 0xD6: case 0xD7:
			/* external drive stuff */
			if (offset & 1)
				apple3_flags |= VAR_EXTA0 << ((offset - 0xD0) / 2);
			else
				apple3_flags &= ~(VAR_EXTA0 << ((offset - 0xD0) / 2));
			apple3_update_drives((device_config*)devtag_get_device(space->machine, "fdc"));
			break;

		case 0xDB:
			apple3_write_charmem(space->machine);
			break;

		case 0xE0: case 0xE1: case 0xE2: case 0xE3:
		case 0xE4: case 0xE5: case 0xE6: case 0xE7:
		case 0xE8: case 0xE9: case 0xEA: case 0xEB:
		case 0xEC: case 0xED: case 0xEE: case 0xEF:
			applefdc_w(fdc, offset, data);
			break;

		case 0xF0:
		case 0xF1:
		case 0xF2:
		case 0xF3:
			acia_6551_w(acia, offset & 0x03, data);
			break;
	}
}

INTERRUPT_GEN( apple3_interrupt )
{
	const device_config *via_1 = devtag_get_device(device->machine, "via6522_1");

	via_ca2_w(via_1, 0, (AY3600_keydata_strobe_r() & 0x80) ? 1 : 0);
	via_cb1_w(via_1, 0, video_screen_get_vblank(device->machine->primary_screen));
	via_cb2_w(via_1, 0, video_screen_get_vblank(device->machine->primary_screen));
}



static UINT8 *apple3_bankaddr(UINT16 bank, offs_t offset)
{
	if (bank != (UINT16) ~0)
	{
		bank %= mess_ram_size / 0x8000;
		if ((bank + 1) == (mess_ram_size / 0x8000))
			bank = 0x02;
	}
	offset += ((offs_t) bank) * 0x8000;
	offset %= mess_ram_size;
	return &mess_ram[offset];
}



static void apple3_setbank(running_machine *machine,int mame_bank, UINT16 bank, offs_t offset)
{
	UINT8 *ptr;

	ptr = apple3_bankaddr(bank, offset);
	memory_set_bankptr(machine, mame_bank, ptr);

	if (LOG_MEMORY)
	{
		#ifdef PTR64
		logerror("\tbank #%d --> %02x/%04x [0x%08lx]\n", mame_bank, (unsigned) bank, (unsigned)offset, ptr - mess_ram);
		#else
		logerror("\tbank #%d --> %02x/%04x [0x%08x]\n", mame_bank, (unsigned) bank, (unsigned)offset, ptr - mess_ram);
		#endif
	}
}



static UINT8 *apple3_get_zpa_addr(offs_t offset)
{
	zpa = (((offs_t) via_0_b) * 0x100) + offset;

	if (via_0_b < 0x20)
		return apple3_bankaddr(~0, zpa);
	else if (via_0_b > 0x9F)
		return apple3_bankaddr(~0, zpa - 0x8000);
	else
		return apple3_bankaddr(via_1_a, zpa - 0x2000);
}



READ8_HANDLER( apple3_00xx_r )
{
	return *apple3_get_zpa_addr(offset);
}



WRITE8_HANDLER( apple3_00xx_w )
{
	*apple3_get_zpa_addr(offset) = data;
}



static void apple3_update_memory(running_machine *machine)
{
	UINT16 bank;
	UINT8 page;
	const address_space* space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);

	if (LOG_MEMORY)
	{
		logerror("apple3_update_memory(): via_0_b=0x%02x via_1_a=0x0x%02x\n", via_0_b, via_1_a);
	}

	cputag_set_clock(machine, "maincpu", (via_0_a & 0x80) ? 1000000 : 2000000);

	/* bank 2 (0100-01FF) */
	if (!(via_0_a & 0x04))
	{
		if (via_0_b < 0x20)
		{
			bank = ~0;	/* system bank */
			page = via_0_b ^ 0x01;
		}
		else if (via_0_b >= 0xA0)
		{
			bank = ~0;	/* system bank */
			page = (via_0_b ^ 0x01) - 0x80;
		}
		else
		{
			bank = via_1_a;
			page = (via_0_b ^ 0x01) - 0x20;
		}
	}
	else
	{
		bank = ~0;
		page = 0x01;
	}
	apple3_setbank(machine,2, bank, ((offs_t) page) * 0x100);

	/* bank 3 (0200-1FFF) */
	apple3_setbank(machine,3, ~0, 0x0200);

	/* bank 4 (2000-9FFF) */
	apple3_setbank(machine,4, via_1_a, 0x0000);

	/* bank 5 (A000-BFFF) */
	apple3_setbank(machine,5, ~0, 0x2000);

	/* install bank 8 (C000-CFFF) */
	if (via_0_a & 0x40)
	{
		memory_install_read8_handler(space, 0xC000, 0xC0FF, 0, 0, apple3_c0xx_r);
		memory_install_write8_handler(space, 0xC000, 0xC0FF, 0, 0, apple3_c0xx_w);
	}
	else
	{
		memory_install_read8_handler(space, 0xC000, 0xC0FF, 0, 0, SMH_BANK(8));
		if (via_0_a & 0x08)
			memory_install_write8_handler(space, 0xC000, 0xC0FF, 0, 0, SMH_UNMAP);
		else
			memory_install_write8_handler(space, 0xC000, 0xC0FF, 0, 0, SMH_BANK(8));
		apple3_setbank(machine,8, ~0, 0x4000);
	}

	/* install bank 9 (C100-C4FF) */
	if (via_0_a & 0x40)
	{
		memory_install_read8_handler(space, 0xC100, 0xC4FF, 0, 0, SMH_NOP);
		memory_install_write8_handler(space, 0xC100, 0xC4FF, 0, 0, SMH_NOP);
	}
	else
	{
		memory_install_read8_handler(space, 0xC100, 0xC4FF, 0, 0, SMH_BANK(9));
		if (via_0_a & 0x08)
			memory_install_write8_handler(space, 0xC100, 0xC4FF, 0, 0, SMH_UNMAP);
		else
			memory_install_write8_handler(space, 0xC100, 0xC4FF, 0, 0, SMH_BANK(9));
		apple3_setbank(machine,9, ~0, 0x4100);
	}

	/* install bank 10 (C500-C7FF) */
	memory_install_read8_handler(space, 0xC500, 0xC7FF, 0, 0, SMH_BANK(10));
	if (via_0_a & 0x08)
		memory_install_write8_handler(space, 0xC500, 0xC7FF, 0, 0, SMH_UNMAP);
	else
		memory_install_write8_handler(space, 0xC500, 0xC7FF, 0, 0, SMH_BANK(10));
	apple3_setbank(machine,10, ~0, 0x4500);

	/* install bank 11 (C800-CFFF) */
	if (via_0_a & 0x40)
	{
		memory_install_read8_handler(space, 0xC800, 0xCFFF, 0, 0, SMH_NOP);
		memory_install_write8_handler(space, 0xC800, 0xCFFF, 0, 0, SMH_NOP);
	}
	else
	{
		memory_install_read8_handler(space, 0xC800, 0xCFFF, 0, 0, SMH_BANK(11));
		if (via_0_a & 0x08)
			memory_install_write8_handler(space, 0xC800, 0xCFFF, 0, 0, SMH_UNMAP);
		else
			memory_install_write8_handler(space, 0xC800, 0xCFFF, 0, 0, SMH_BANK(11));
		apple3_setbank(machine,11, ~0, 0x4800);
	}

	/* install bank 6 (D000-EFFF) */
	memory_install_read8_handler(space, 0xD000, 0xEFFF, 0, 0, SMH_BANK(6));
	if (via_0_a & 0x08)
		memory_install_write8_handler(space, 0xD000, 0xEFFF, 0, 0, SMH_UNMAP);
	else
		memory_install_write8_handler(space, 0xD000, 0xEFFF, 0, 0, SMH_BANK(6));
	apple3_setbank(machine,6, ~0, 0x5000);

	/* install bank 7 (F000-FFFF) */
	memory_install_read8_handler(space, 0xF000, 0xFFFF, 0, 0, SMH_BANK(7));
	if (via_0_a & 0x09)
		memory_install_write8_handler(space, 0xF000, 0xFFFF, 0, 0, SMH_UNMAP);
	else
		memory_install_write8_handler(space, 0xF000, 0xFFFF, 0, 0, SMH_BANK(7));
	if (via_0_a & 0x01)
		memory_set_bankptr(machine,7, memory_region(machine, "maincpu"));
	else
		apple3_setbank(machine,7, ~0, 0x7000);

	/* reinstall VIA handlers */
	{
		const device_config *via_0 = devtag_get_device(space->machine, "via6522_0");
		const device_config *via_1 = devtag_get_device(space->machine, "via6522_1");
		memory_install_read8_device_handler(space, via_0, 0xFFD0, 0xFFDF, 0, 0, via_r);
		memory_install_write8_device_handler(space, via_0, 0xFFD0, 0xFFDF, 0, 0, via_w);
		memory_install_read8_device_handler(space, via_1, 0xFFE0, 0xFFEF, 0, 0, via_r);
		memory_install_write8_device_handler(space, via_1, 0xFFE0, 0xFFEF, 0, 0, via_w);
	}
}



static void apple3_via_out(running_machine *machine, UINT8 *var, UINT8 data)
{
	if (*var != data)
	{
		*var = data;
		apple3_update_memory(machine);
	}
}


/* these are here to appease the Apple /// confidence tests */
static READ8_DEVICE_HANDLER(apple3_via_1_in_a) { return ~0; }
static READ8_DEVICE_HANDLER(apple3_via_1_in_b) { return ~0; }

static WRITE8_DEVICE_HANDLER(apple3_via_0_out_a) { apple3_via_out(device->machine, &via_0_a, data); }
static WRITE8_DEVICE_HANDLER(apple3_via_0_out_b) { apple3_via_out(device->machine, &via_0_b, data); }
static WRITE8_DEVICE_HANDLER(apple3_via_1_out_a) { apple3_via_out(device->machine, &via_1_a, data); }
static WRITE8_DEVICE_HANDLER(apple3_via_1_out_b) { apple3_via_out(device->machine, &via_1_b, data); }

static void apple2_via_1_irq_func(const device_config *device, int state)
{
	if (!via_1_irq && state)
	{
		cputag_set_input_line(device->machine, "maincpu", M6502_IRQ_LINE, ASSERT_LINE);
		cputag_set_input_line(device->machine, "maincpu", M6502_IRQ_LINE, CLEAR_LINE);
	}
	via_1_irq = state;
}

const via6522_interface apple3_via_0_intf =
{
	DEVCB_NULL,					/* in_a_func */
	DEVCB_NULL,					/* in_b_func */
	DEVCB_NULL,					/* in_ca1_func */
	DEVCB_NULL,					/* in_cb1_func */
	DEVCB_NULL,					/* in_ca2_func */
	DEVCB_NULL,					/* in_cb2_func */
	DEVCB_HANDLER(apple3_via_0_out_a),		/* out_a_func */
	DEVCB_HANDLER(apple3_via_0_out_b),		/* out_b_func */
	DEVCB_NULL,					/* out_ca1_func */
	DEVCB_NULL,					/* out_cb1_func */
	DEVCB_NULL,					/* out_ca2_func */
	DEVCB_NULL,					/* out_cb2_func */
	DEVCB_NULL					/* irq_func */
};

const via6522_interface apple3_via_1_intf =
{
	DEVCB_HANDLER(apple3_via_1_in_a),		/* in_a_func */
	DEVCB_HANDLER(apple3_via_1_in_b),		/* in_b_func */
	DEVCB_NULL,					/* in_ca1_func */
	DEVCB_NULL,					/* in_cb1_func */
	DEVCB_NULL,					/* in_ca2_func */
	DEVCB_NULL,					/* in_cb2_func */
	DEVCB_HANDLER(apple3_via_1_out_a),		/* out_a_func */
	DEVCB_HANDLER(apple3_via_1_out_b),		/* out_b_func */
	DEVCB_NULL,					/* out_ca1_func */
	DEVCB_NULL,					/* out_cb1_func */
	DEVCB_NULL,					/* out_ca2_func */
	DEVCB_NULL,					/* out_cb2_func */
	DEVCB_LINE(apple2_via_1_irq_func)	/* irq_func */
};



MACHINE_RESET( apple3 )
{
}



static UINT8 *apple3_get_indexed_addr(offs_t offset)
{
	UINT8 n;
	UINT8 *result = NULL;

	if ((via_0_b >= 0x18) && (via_0_b <= 0x1F))
	{
		n = *apple3_bankaddr(~0, zpa ^ 0x0C00);

		if (LOG_INDXADDR)
		{
			static UINT8 last_n;
			if (last_n != n)
			{
				logerror("indxaddr: zpa=0x%04x n=0x%02x\n", zpa, n);
				last_n = n;
			}
		}

		if (n == 0x8F)
		{
			/* get at that special ram under the VIAs */
			if ((offset >= 0xFFD0) && (offset <= 0xFFEF))
				result = apple3_bankaddr(~0, offset & 0x7FFF);
			else if (offset < 0x2000)
				result = apple3_bankaddr(~0, offset - 0x2000);
			else if (offset > 0x9FFF)
				result = apple3_bankaddr(~0, offset - 0x8000);
			else
				result = &mess_ram[offset - 0x2000];
		}
		else if ((n >= 0x80) && (n <= 0x8E))
		{
			if (offset < 0x0100)
				result = apple3_bankaddr(~0, ((offs_t) via_0_b) * 0x100 + offset);
			else
				result = apple3_bankaddr(n, offset);
		}
		else if (n == 0xFF)
		{
			if (offset < 0x2000)
				result = apple3_bankaddr(~0, offset - 0x2000);
			else if (offset < 0xA000)
				result = apple3_bankaddr(via_1_a, offset - 0x2000);
			else if (offset < 0xC000)
				result = apple3_bankaddr(~0, offset - 0x8000);
			else if (offset < 0xD000)
				result = NULL;
			else if (offset < 0xF000)
				result = apple3_bankaddr(~0, offset - 0x8000);
			else
				result = (UINT8 *) ~0;
		}
		else if (offset < 0x0100)
		{
			result = apple3_bankaddr(~0, ((offs_t) via_0_b) * 0x100 + offset);
		}
	}
	else if ((offset >= 0xF000) && (via_0_a & 0x01))
	{
#if 0
		/* The Apple /// Diagnostics seems to expect that indexed writes
		 * always write to RAM.  That image jumps to an address that is
		 * undefined unless this code is enabled.  However, the Sara
		 * emulator does not have corresponding code here, though Chris
		 * Smolinski does not rule out the possibility
		 */
		result = apple3_bankaddr(~0, offset - 0x8000);
#endif
	}

	return result;
}



static READ8_HANDLER( apple3_indexed_read )
{
	UINT8 result;
	UINT8 *addr;

	addr = apple3_get_indexed_addr(offset);
	if (!addr)
		result = memory_read_byte(space, offset);
	else if (addr != (UINT8 *) ~0)
		result = *addr;
	else
		result = memory_region(space->machine, "maincpu")[offset % memory_region_length(space->machine, "maincpu")];
	return result;
}



static WRITE8_HANDLER( apple3_indexed_write )
{
	UINT8 *addr;

	addr = apple3_get_indexed_addr(offset);
	if (!addr)
		memory_write_byte(space, offset, data);
	else if (addr != (UINT8 *) ~0)
		*addr = data;
}



static DIRECT_UPDATE_HANDLER( apple3_opbase )
{
	UINT8 *opptr;

	if ((address & 0xFF00) == 0x0000)
	{
		opptr = apple3_get_zpa_addr(address);
		direct->bytemask = ~0;
		direct->raw = direct->decrypted = opptr - address;
		direct->bytestart = address;
		direct->byteend = address;
		address = ~0;
	}
	return address;
}



static void apple3_update_drives(const device_config *device)
{
	int enable_mask = 0x00;

	if (apple3_enable_mask & 0x01)
		enable_mask |= 0x01;

	if (apple3_enable_mask & 0x02)
	{
		switch(apple3_flags & (VAR_EXTA0 | VAR_EXTA1))
		{
			case VAR_EXTA0:
				enable_mask |= 0x02;
				break;
			case VAR_EXTA1:
				enable_mask |= 0x04;
				break;
			case VAR_EXTA1|VAR_EXTA0:
				enable_mask |= 0x08;
				break;
		}
	}

	apple525_set_enable_lines(device,enable_mask);
}



static void apple3_set_enable_lines(const device_config *device,int enable_mask)
{
	apple3_enable_mask = enable_mask;
	apple3_update_drives(device);
}



const applefdc_interface apple3_fdc_interface =
{
	apple525_set_lines,
	apple3_set_enable_lines,
	apple525_read_data,
	apple525_write_data
};



DRIVER_INIT( apple3 )
{
	/* hack to get around VIA problem */
	memory_region(machine, "maincpu")[0x0685] = 0x00;

	apple3_enable_mask = 0;
	apple3_update_drives((device_config*)devtag_get_device(machine, "fdc"));

	AY3600_init(machine);

	apple3_profile_init();

	apple3_flags = 0;
	via_0_a = ~0;
	via_1_a = ~0;
	via_1_irq = 0;
	apple3_update_memory(machine);

	memory_set_direct_update_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), apple3_opbase);

	/* the Apple /// does some weird tricks whereby it monitors the SYNC pin
	 * on the CPU to check for indexed instructions and directs them to
	 * different memory locations */
	device_set_info_fct(cputag_get_cpu(machine, "maincpu"), CPUINFO_FCT_M6502_READINDEXED_CALLBACK, (genf *) apple3_indexed_read);
	device_set_info_fct(cputag_get_cpu(machine, "maincpu"), CPUINFO_FCT_M6502_WRITEINDEXED_CALLBACK, (genf *) apple3_indexed_write);
}
