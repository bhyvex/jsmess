/***************************************************************************

        MC-80

        12/05/2009 Skeleton driver.

****************************************************************************/

#include "driver.h"
#include "cpu/z80/z80.h"

static ADDRESS_MAP_START(mc80_mem, ADDRESS_SPACE_PROGRAM, 8)
	ADDRESS_MAP_UNMAP_HIGH
ADDRESS_MAP_END

static ADDRESS_MAP_START( mc80_io , ADDRESS_SPACE_IO, 8)
	ADDRESS_MAP_UNMAP_HIGH
ADDRESS_MAP_END

/* Input ports */
INPUT_PORTS_START( mc80 )
INPUT_PORTS_END


static MACHINE_RESET(mc80)
{
}

static VIDEO_START( mc80 )
{
}

static VIDEO_UPDATE( mc80 )
{
    return 0;
}

static MACHINE_DRIVER_START( mc80 )
    /* basic machine hardware */
    MDRV_CPU_ADD("maincpu",Z80, XTAL_4MHz)
    MDRV_CPU_PROGRAM_MAP(mc80_mem, 0)
    MDRV_CPU_IO_MAP(mc80_io, 0)

    MDRV_MACHINE_RESET(mc80)

    /* video hardware */
    MDRV_SCREEN_ADD("screen", RASTER)
    MDRV_SCREEN_REFRESH_RATE(50)
    MDRV_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
    MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
    MDRV_SCREEN_SIZE(640, 480)
    MDRV_SCREEN_VISIBLE_AREA(0, 640-1, 0, 480-1)
    MDRV_PALETTE_LENGTH(2)
    MDRV_PALETTE_INIT(black_and_white)

    MDRV_VIDEO_START(mc80)
    MDRV_VIDEO_UPDATE(mc80)
MACHINE_DRIVER_END

static SYSTEM_CONFIG_START(mc80)
SYSTEM_CONFIG_END

/* ROM definition */
ROM_START( mc80 )
    ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASEFF )
  ROM_LOAD( "zve1a.rom", 0x0000, 0x0400, CRC(f7050697) SHA1(d1bc032ce8be1cc82c7292e75d6050c6236f55e9))
  ROM_LOAD( "zve1i.rom", 0x0000, 0x0400, CRC(26f3cab7) SHA1(dd20f19ac25c904fab0fd3291b125cbb30dc1f6a))
  ROM_LOAD( "zve1m.rom", 0x0000, 0x0400, CRC(423f7d85) SHA1(a21636fde4fa9ca13fb6d65a0926e236cf84b41c))
  ROM_LOAD( "zve2a.rom", 0x0000, 0x0400, CRC(976e9f90) SHA1(2e924a3e808bc074ebce32b7f9172ec2c85b5431))
  ROM_LOAD( "zve2i.rom", 0x0000, 0x0400, CRC(d76c53ea) SHA1(b389a389f859a2b9561937220954f2294bbacab8))
  ROM_LOAD( "zve2m.rom", 0x0000, 0x0400, CRC(ced7baf9) SHA1(3d1673e332ea0e2710b0f8e07e7c68a12d846973))
  ROM_LOAD( "mo01.rom", 0x0000, 0x0400, CRC(c65a470f) SHA1(71325fed1a342149b5efc2234ecfc8adfff0a42d))
  ROM_LOAD( "mo03.rom", 0x0000, 0x0400, CRC(29685056) SHA1(39e77658fb7af5a28112341f0893e007d73c1b7a))
  ROM_LOAD( "mo04.rom", 0x0000, 0x0400, CRC(fd315b73) SHA1(cfb943ec8c884a9b92562d05f92faf06fe42ad68))
  ROM_LOAD( "mo05.rom", 0x0000, 0x0400, CRC(453d6370) SHA1(d96f0849a2da958d7e92a31667178ad140719477))
  ROM_LOAD( "mo06.rom", 0x0000, 0x0400, CRC(6357aba5) SHA1(a4867766f6e14e9fe66f22a6839f17c01058c8af))
  ROM_LOAD( "mo07.rom", 0x0000, 0x0400, CRC(a1eb6021) SHA1(b05b63f02de89f065f337bbe54c5b48244e3a4ba))
  ROM_LOAD( "mo08.rom", 0x0000, 0x0400, CRC(8221cc32) SHA1(65e0ee4241d39d138205c88374b3bcd127e21511))
  ROM_LOAD( "mo09.rom", 0x0000, 0x0400, CRC(7ad5944d) SHA1(ef2781b114277a09ce0cf2e7decfdb7c48a693e3))
  ROM_LOAD( "mo10.rom", 0x0000, 0x0400, CRC(11de8c76) SHA1(b384d22506ff7e3e28bd2dcc33b7a69617eeb52a))
  ROM_LOAD( "mo11.rom", 0x0000, 0x0400, CRC(370cc672) SHA1(133f6e8bfd4e1ca2e9e0a8e2342084419f895e3c))
  ROM_LOAD( "mo12.rom", 0x0000, 0x0400, CRC(a3838f2b) SHA1(e3602943700bf5068117946bf86f052f5c587169))
  ROM_LOAD( "mo13.rom", 0x0000, 0x0400, CRC(88b61d12) SHA1(00dd4452b4d4191e589ab58ba924ed21b10f323b))
  ROM_LOAD( "mo14.rom", 0x0000, 0x0400, CRC(2168da19) SHA1(c1ce1263167067d8be0a90604d9c29b7379a0545))
  ROM_LOAD( "mo15.rom", 0x0000, 0x0400, CRC(e32f54c4) SHA1(c3d9ca2204e7adbc625cf96031acb8c1df0447c7))
  ROM_LOAD( "mo16.rom", 0x0000, 0x0400, CRC(403be935) SHA1(4e74355a78ab090ce180437156fed8e4a1d1c787))

ROM_END

/* Driver */

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    INIT    CONFIG COMPANY   FULLNAME       FLAGS */
COMP( ????, mc80,  0,       0, 	mc80, 	mc80, 	 0,  	  mc80,  	 "VEB Elektronik Gera",   "MC-80",		GAME_NOT_WORKING)

