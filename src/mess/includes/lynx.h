/*****************************************************************************
 *
 * includes/lynx.h
 *
 ****************************************************************************/

#ifndef LYNX_H_
#define LYNX_H_

#include "sound/custom.h"
#include "devices/cartslot.h"


#define LYNX_CART		0
#define LYNX_QUICKLOAD	1


/*----------- defined in machine/lynx.c -----------*/

MACHINE_START( lynx );

extern UINT8 *lynx_mem_0000;
extern UINT8 *lynx_mem_fc00;
extern UINT8 *lynx_mem_fd00;
extern UINT8 *lynx_mem_fe00;
extern UINT8 *lynx_mem_fffa;
extern size_t lynx_mem_fe00_size;

READ8_HANDLER( lynx_memory_config_r );
WRITE8_HANDLER( lynx_memory_config_w );
WRITE8_HANDLER(mikey_write);
READ8_HANDLER(mikey_read);
WRITE8_HANDLER(suzy_write);
READ8_HANDLER(suzy_read);
void lynx_timer_count_down(running_machine *machine, int nr);

INTERRUPT_GEN( lynx_frame_int );

/* These functions are also needed for the Quickload */
int lynx_verify_cart (char *header, int kind);
void lynx_crc_keyword(const device_config *image);

extern const cartslot_interface lynx_cartslot;


/*----------- defined in audio/lynx.c -----------*/

void lynx_audio_reset(void);
void lynx_audio_write(int offset, UINT8 data);
UINT8 lynx_audio_read(int offset);
void lynx_audio_count_down(running_machine *machine, int nr);
CUSTOM_START( lynx_custom_start );
CUSTOM_START( lynx2_custom_start );


#endif /* LYNX_H_ */
