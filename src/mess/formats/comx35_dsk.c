/*********************************************************************

	formats/comx35_dsk.c

	COMX35 disk images

*********************************************************************/

#include <string.h>

#include "formats/comx35_dsk.h"
#include "formats/basicdsk.h"

static FLOPPY_IDENTIFY(comx35_dsk_identify)
{
	*vote = ((floppy_image_size(floppy) == (35*1*16*128)) || (floppy_image_size(floppy) == (35*2*16*128))) ? 100 : 0;
	return FLOPPY_ERROR_SUCCESS;
}

static FLOPPY_CONSTRUCT(comx35_dsk_construct)
{
	struct basicdsk_geometry geometry;
	UINT8 header[1];
	int heads;
	int cylinders;	
	
	switch(floppy_image_size(floppy)) {
		case 35*1*16*128:
			heads = 1;
			cylinders = 35;			
			break;

		case 35*2*16*128:			
			floppy_image_read( floppy, header, 0x12 , 1 );
			if (header[0] == 0x01)
			{				
				heads = 1;
				cylinders = 70;
			}
			else
			{
				heads = 2;
				cylinders = 35;
			}
			break;		
	}
	
	memset(&geometry, 0, sizeof(geometry));
	geometry.heads = heads;
	geometry.first_sector_id = 0;
	geometry.sector_length = 128;
	geometry.tracks = cylinders;
	geometry.sectors = 16;
	return basicdsk_construct(floppy, &geometry);	
}

/* ----------------------------------------------------------------------- */

FLOPPY_OPTIONS_START( comx35 )
	FLOPPY_OPTION( comx35, "img",		"COMX35 floppy disk image",	comx35_dsk_identify, comx35_dsk_construct, NULL)
FLOPPY_OPTIONS_END

